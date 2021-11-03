// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include "flexalloc_freelist.h"
#include "flexalloc_bits.h"
#include "flexalloc_util.h"
#include "flexalloc_tests_common.h"

#define FLIST_ENTRY(f, n) &(f)[1 + n]

#define ASSERT_FLIST_LEN(flist, len)            \
  FLA_ASSERTF(fla_flist_len(flist) == len, "got: %"PRIu32, fla_flist_len(flist))

#define ASSERT_WORD(flist, ndx, val)            \
  FLA_ASSERTF(*FLIST_ENTRY(flist, ndx) == val, "{word: %"PRIu32", expected: %"PRIu32", got: %"PRIu32"}", ndx, val, *FLIST_ENTRY(flist, ndx))

void
print_binary(size_t const size, void const * const ptr)
{
  unsigned char *b = (unsigned char*) ptr;
  unsigned char byte;
  int i, j;

  for (i = size-1; i >= 0; i--)
  {
    for (j = 7; j >= 0; j--)
    {
      byte = (b[i] >> j) & 1;
      printf("%u", byte);
    }
    printf(" ");
  }
  puts("");
}

void
pp_uint32_t(uint32_t *v)
{
  print_binary(sizeof(uint32_t), v);
}

uint32_t
binval(const char *bstring)
{
  /*
   * converts a string of 1's and 0's into is equivalent binary value.
   */
  uint32_t ret = 0;

  for (int i = 0; i < strlen(bstring); i++)
  {
    char c = bstring[i];
    if (c == ' ') continue;
    if (c == '0')
    {
      ret <<= 1;
    }
    else if (c == '1')
    {
      ret = (ret << 1) | 1;
    }
  }

  return ret;
}

void
_print_flist(freelist_t flist)
{
  uint32_t elems = FLA_FREELIST_U32_ELEMS(*flist);
  for (uint32_t i = 0; i < elems; i++)
  {
    fprintf(stdout, "   elem %2d: ", i);
    print_binary(sizeof(uint32_t), &flist[1 + i]);
  }
}

int
test_byte_val()
{
  // sanity-check - using this helper for multiple tests
  int err = 0;
  err |= FLA_ASSERT(binval("0") == 0, "binval failure");
  err |= FLA_ASSERT(binval("1") == 1, "binval failure");
  err |= FLA_ASSERT(binval("11") == 3, "binval failure");
  err |= FLA_ASSERT(binval("10") == 2, "binval failure");
  err |= FLA_ASSERT(binval("1 1111 1111") == 511, "binval failure");
  return err;
}

int
test_flist_init_helper(uint32_t len, uint32_t last_word)
{
  int err;
  uint32_t words = FLA_FREELIST_U32_ELEMS(len);
  freelist_t f = NULL;

  if ((err = FLA_ASSERT(!fla_flist_new(len, &f), "failed to create freelist")))
    return err;

  if((err = FLA_ASSERT(f != 0, "expected a non-null reference")))
  {
    goto exit;
  }

  if ((err = ASSERT_FLIST_LEN(f, len)))
    goto exit;

  for (unsigned int i = 0; i < (words-1); i++)
  {
    if ((err = ASSERT_WORD(f, i, UINT32_MAX)))
      goto exit;
  }

  if ((err = ASSERT_WORD(f, words-1, last_word)))
    goto exit;

exit:
  if (err && f)
    _print_flist(f);
  if (f) free(f);
  return err;
}

int
flist_eql(freelist_t f1, freelist_t f2)
{
  int num_words = FLA_FREELIST_U32_ELEMS(fla_flist_len(f1));
  if (fla_flist_len(f1) != fla_flist_len(f2))
  {
    return 0;
  }

  for (unsigned int i = 0; i < num_words; i++)
  {
    if (*FLIST_ENTRY(f1, i) != *FLIST_ENTRY(f2, i))
    {
      return 0;
    }
  }
  return 1;
}

int
test_flist_reset(uint32_t len)
{
  freelist_t f1 = NULL, f2 = NULL;
  int err = 0;

  if ((err = FLA_ASSERT(!fla_flist_new(len, &f1), "failed to create freelist")))
    return err;

  if((err = FLA_ASSERT(f1 != 0, "expected a non-null reference")))
  {
    goto exit;
  }

  if ((err = FLA_ASSERT(!fla_flist_new(len, &f2), "failed to create freelist")))
    return err;

  if((err = FLA_ASSERT(f2 != 0, "expected a non-null reference")))
  {
    goto exit;
  }

  if ((err = FLA_ASSERT(flist_eql(f1, f2) != 0, "expected freelists to be equal")))
    goto exit;

  for (unsigned int i = 0; i < len; i++)
  {
    if ((err = FLA_ASSERTF(fla_flist_entries_alloc(f2, 1) == i,
                           "expected allocation %u to succeed", i)))
      goto exit;
  }

  if ((err = FLA_ASSERT(!flist_eql(f1, f2), "expected freelists to differ after allocations")))
    goto exit;

  fla_flist_reset(f2);

  if ((err = FLA_ASSERT(flist_eql(f1, f2) != 0, "expected freelists to be equal")))
    goto exit;

exit:
  if (f1) free(f1);
  if (f2) free(f2);
  return err;
}

int
test_flist_37_alloc_free_max()
{
  int err;
  freelist_t f = NULL;

  if ((err = FLA_ASSERT(!fla_flist_new(37, &f), "failed to create freelist")))
    return err;

  if((err = FLA_ASSERT(f != 0, "expected a non-null reference")))
  {
    goto exit;
  }

  if ((err = ASSERT_FLIST_LEN(f, 37)))
    goto exit;

  // first entry is fully free
  if ((err = ASSERT_WORD(f, 0, UINT32_MAX)))
    goto exit;

  if ((err = ASSERT_WORD(f, 1, 31))) // 0b11111
    goto exit;

  // allocate all spaces
  for (unsigned int i = 0; i < 37; i++)
    err |= FLA_ASSERTF(fla_flist_entries_alloc(f, 1) == i, "alloc failed for i=%u", i);

  FLA_ASSERT(fla_flist_entries_alloc(f, 1) == -1, "expected failure to allocate");

  FLA_ASSERT(fla_flist_entry_free(f, 0) == 0, "expected free to work");
  FLA_ASSERT(fla_flist_entry_free(f, 1) == 0, "expected free to work");
  // only two lowest entries freed, e.g. 00000000 00000000 00000000 00000011
  FLA_ASSERTF(*FLIST_ENTRY(f, 0) == 3, "got: %"PRIu32, *FLIST_ENTRY(f, 0));

exit:
  if (f) free(f);
  return err;
}

int
test_alloc_free_deep()
{
  int err;
  freelist_t f = NULL;

  if ((err = FLA_ASSERT(!fla_flist_new(8, &f), "failed to create freelist")))
    return err;

  if((err = FLA_ASSERT(f != 0, "expected a non-null reference")))
  {
    goto exit;
  }

  if ((err = ASSERT_FLIST_LEN(f, 8)))
    goto exit;

  if ((err = ASSERT_WORD(f, 0, binval("1111 1111"))))
    goto exit;

  // allocate all spaces
  for (unsigned int i = 0; i < 8; i++)
    err |= FLA_ASSERTF(fla_flist_entries_alloc(f, 1) == i, "alloc failed for i=%u", i);

  err |= FLA_ASSERT(fla_flist_entries_alloc(f, 1) == -1, "expected failure to allocate");
  err |= FLA_ASSERT(*FLIST_ENTRY(f, 0) == 0, "expected all entries taken");

  err |= FLA_ASSERT(fla_flist_entry_free(f, 0) == 0, "expected free to work");
  err |= FLA_ASSERT(fla_flist_entry_free(f, 1) == 0, "expected free to work");
  // only two lowest entries freed, e.g. 00000000 00000000 00000000 00000011
  err |= FLA_ASSERTF(*FLIST_ENTRY(f, 0) == binval("11"), "got: %"PRIu32, *FLIST_ENTRY(f, 0));

  err |= FLA_ASSERT(fla_flist_entry_free(f, 7) == 0, "expected free to work");
  err |= FLA_ASSERTF(*FLIST_ENTRY(f, 0) == binval("1000 0011"), "got: %"PRIu32, *FLIST_ENTRY(f,
                     0));

  err |= FLA_ASSERT(fla_flist_entry_free(f, 5) == 0, "expected free to work");
  err |= FLA_ASSERTF(*FLIST_ENTRY(f, 0) == binval("1010 0011"), "got: %"PRIu32, *FLIST_ENTRY(f,
                     0));

  // re-alloc the free entries
  err |= FLA_ASSERT(fla_flist_entries_alloc(f, 1) == 0, "unexpected alloc");
  err |= FLA_ASSERT(fla_flist_entries_alloc(f, 1) == 1, "unexpected alloc");
  err |= FLA_ASSERT(fla_flist_entries_alloc(f, 1) == 5, "unexpected alloc");
  err |= FLA_ASSERT(fla_flist_entries_alloc(f, 1) == 7, "unexpected alloc");

exit:
  if (f) free(f);
  return err;
}


int
main(int argc, char **argv)
{
  int err = 0;

  if (test_byte_val())
    return 1;

  err |= test_flist_init_helper(1, 1);
  err |= test_flist_init_helper(4, 15);
  err |= test_flist_init_helper(32, UINT32_MAX);
  err |= test_flist_init_helper(33, 1);

  // establish that alloc and free works
  err |= test_flist_37_alloc_free_max();
  err |= test_alloc_free_deep();

  // no sense continuing if entry alloc/free seem broken
  if (err) return err;

  err |= test_flist_reset(1);
  err |= test_flist_reset(4);
  err |= test_flist_reset(32);
  err |= test_flist_reset(33);

  return err;
}
