#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include "flexalloc_freelist.h"
#include "flexalloc_bits.h"
#include "flexalloc_util.h"

size_t
fla_flist_size(uint32_t len)
{
  // the leading uint32_t element keeps the length of the freelist
  return sizeof(uint32_t) * (1 + FLA_FREELIST_U32_ELEMS(len));
}

uint32_t
fla_flist_len(freelist_t flist)
{
  return *flist;
}

uint32_t
fla_flist_num_reserved(freelist_t flist)
{
  uint32_t *ptr = flist + 1;
  uint32_t *end = flist + fla_flist_size(*flist) / sizeof(uint32_t);
  uint32_t free_entries = 0;

  for (; ptr != end; ptr++)
  {
    free_entries += count_set_bits(*ptr);
  }
  return *flist - free_entries;
}

void
fla_flist_reset(freelist_t flist)
{
  uint32_t *len = flist;
  uint32_t elems = FLA_FREELIST_U32_ELEMS(*flist);
  uint32_t *elem = flist + 1;
  uint32_t *elem_last = elem + elems - 1;
  uint32_t unused_spots;

  /*
   * Initialize freelist by writing 1's for every empty space.
   * Because the freelist is backed by an array of uint32_t values, it is likely
   * that the sum of available bits (capacity) exceed the desired size of the
   * freelist.
   * That is why we compute the number of 'unused_spots' and ensure these bits
   * start as 0 (reserved).
   */
  for (; elem != elem_last; elem++)
  {
    *elem = ~0;
  }
  unused_spots = elems * sizeof(uint32_t) * 8 - *len;
  *elem = (~0u) >> unused_spots;
}

void
fla_flist_init(freelist_t flist, uint32_t len)
{
  flist[0] = len;
  fla_flist_reset(flist);
}

int
fla_flist_new(uint32_t len, freelist_t *flist)
{
  if (len == 0)
    return FLA_ERR(-EINVAL, "fla_flist_init(), Cannot have zero len");

  *flist = malloc(fla_flist_size(len));
  if (FLA_ERR(!(*flist), "malloc()"))
    return -ENOMEM;

  fla_flist_init(*flist, len);

  return 0;
}

void
fla_flist_free(freelist_t flist)
{
  free(flist);
}

freelist_t
fla_flist_load(void *data)
{
  return (freelist_t)data;
}

// find and take a spot in the freelist, returning its index
int
fla_flist_entry_alloc(freelist_t flist, uint32_t elems)
{
  uint32_t *elem;
  uint32_t wndx = 0;

  for (uint32_t i = 0; i < elems; i++)
  {
    elem = &flist[1 + i];
    // fully booked
    if (*elem == 0)
      continue;

    // isolate rightmost 1-bit, store in `wndx` that we may calculate the entry's
    // index, then set it in the freelist.
    wndx = *elem & (- *elem);
    *elem &= ~wndx;
    return i * sizeof(uint32_t) * 8 + ntz(wndx);
  }
  return -1;
}

int
fla_flist_entries_alloc(freelist_t flist, unsigned int num)
{
  uint32_t elems = FLA_FREELIST_U32_ELEMS(*flist);
  uint32_t alloc_count;
  int alloc_ret;

  alloc_ret = fla_flist_entry_alloc(flist, elems);

  if (num == 1)
    return alloc_ret;

  for(alloc_count = 1; alloc_count != num; ++alloc_count)
  {
    if(fla_flist_entry_alloc(flist, elems) == -1)
      return -1;
  }

  return alloc_ret;
}

// release a taken element from freelist
int
fla_flist_entry_free(freelist_t flist, uint32_t ndx)
{
  uint32_t *elem = flist + 1;
  if (ndx > *flist)
    return -1;

  while (ndx >= sizeof(uint32_t) * CHAR_BIT)
  {
    elem++;
    ndx -= sizeof(uint32_t) * CHAR_BIT;
  }
  *elem |= 1 << ndx;
  return 0;
}

int
fla_flist_entries_free(freelist_t flist, uint32_t ndx, unsigned int num)
{
  for(uint32_t i = 0 ; i < num ; ++i)
  {
    if(fla_flist_entry_free(flist, ndx+i))
      return -1;
  }
  return 0;
}

int
fla_flist_search_wfunc(freelist_t flist, uint64_t flags, uint32_t *found,
                       int(*f)(const uint32_t, va_list), ...)
{
  int ret;
  uint32_t elem_cpy, wndx = 0;
  uint32_t u32_elems = FLA_FREELIST_U32_ELEMS(*flist);
  uint32_t len = *flist;
  va_list ap;

  if ((flags & FLA_FLIST_SEARCH_FROM_START) == 0)
    return -EINVAL;

  *found = 0;

  for (uint32_t u32_elem = 0 ; u32_elem < u32_elems; u32_elem++)
  {
    elem_cpy = flist[1 + u32_elem];

    /*
     * There is a special case when we reach the end element where all the
     * unused bits are NOT 1s but 0s. Here we need to set the unused 0s to ones
     * so our stopping condition is valid.
     */
    if (u32_elem + 1 == u32_elems)
    {
      uint32_t used_spots = len % 32;
      elem_cpy = elem_cpy | (~0 << used_spots);
    }

    // All free
    if (elem_cpy == 0xFFFFFFFF)
      continue;

    /* For all the zero bits: isolate and exec f on the index. */
    for (uint8_t j = 0; j < 32 && elem_cpy != 0xFFFFFFFF; ++j)
    {
      wndx = ~elem_cpy & (elem_cpy + 1);

      va_start(ap, f);
      ret = f(u32_elem * sizeof(uint32_t) * 8 + ntz(wndx), ap);
      va_end(ap);

      switch (ret)
      {
      case FLA_FLIST_SEARCH_RET_FOUND_STOP:
        *found += 1;
      case FLA_FLIST_SEARCH_RET_STOP:
        return 0;
      case FLA_FLIST_SEARCH_RET_FOUND_CONTINUE:
        *found += 1;
      case FLA_FLIST_SEARCH_RET_CONTINUE:
        elem_cpy |= wndx;
        continue;
      case FLA_FLIST_SEARCH_RET_ERR:
      default:
        FLA_ERR(ret, "fla_flist_search_wfunc(): f() return unknown value");
        return ret;
      }
    }
  }
  return 0;
}
