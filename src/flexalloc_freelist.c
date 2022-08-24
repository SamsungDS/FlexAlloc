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
  *flist = malloc(fla_flist_size(len));
  if (FLA_ERR(!(*flist), "malloc()"))
  {
    return -ENOMEM;
  }

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

  while (ndx > sizeof(uint32_t) * CHAR_BIT)
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
