// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

#include <stdio.h>
#include <inttypes.h>
#include "src/flexalloc_hash.h"
#include "src/flexalloc_util.h"
#include "flexalloc_tests_common.h"

#define HTBL_NEW_ERR "failed to initialize hash table"

void
print_tbl_entry(struct fla_htbl_entry *e, int i)
{
  if (i < 0)
    fprintf(stdout, "{h2: %20"PRIu64", val: %"PRIu32", psl: %"PRIu16"}\n", e->h2, e->val,
            e->psl);
  else
    fprintf(stdout, "%3d: {h2: %20"PRIu64", val: %"PRIu32", psl: %"PRIu16"}\n", i, e->h2, e->val,
            e->psl);
  fflush(stdout);
}

void
print_tbl(struct fla_htbl *htbl)
{
  for (unsigned int i = 0; i < htbl->tbl_size; i++)
  {
    print_tbl_entry(&htbl->tbl[i], i);
  }
}

// naive implementation of how to look up an entry, useful to test the
// more complex implementations
struct fla_htbl_entry *_htbl_get_val(struct fla_htbl *htbl, char *key)
{
  uint64_t h2 = FLA_HTBL_H2(key);
  struct fla_htbl_entry *e;

  for (unsigned int i = 0; i < htbl->tbl_size; i++)
  {
    e = &htbl->tbl[i];

    if (e->h2 == h2)
      return e;
  }
  return NULL;
}

unsigned int
_htbl_len(struct fla_htbl *htbl)
{
  unsigned int entries = 0;
  struct fla_htbl_entry *e;

  for (unsigned int i = 0; i < htbl->tbl_size; i++)
  {
    e = &htbl->tbl[i];
    if (e->h2 != FLA_HTBL_ENTRY_UNSET)
      entries++;
  }
  return entries;
}

int
test_initialization()
{
  struct fla_htbl *htbl;
  int err = 0;

  // test
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->tbl != NULL, "expected ptr to a an allocated array");
  err |= FLA_ASSERT(htbl->len == 0, "expected len==0");
  err |= FLA_ASSERT(_htbl_len(htbl) == 0,
                    "expected all elements to be set to FLA_HTBL_ENTRY_UNSET");
  err |= FLA_ASSERT(htbl->tbl_size == 17, "expected 17 slots");
  htbl_free(htbl);

  return err != 0;
}

int
test_insert_one()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expected 0 entries in table");

  // test
  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expect one entry in table");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  htbl_free(htbl);
  return test_err;
}

int
test_insert_multiple()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // test
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expect 0 entries in table");

  // test
  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expect one entry in table");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  err |= htbl_insert(htbl, "two", 2);
  test_err |= FLA_ASSERT(htbl->len == 2, "expect two entries in table");
  e = _htbl_get_val(htbl, "two");
  test_err |= FLA_ASSERT(e->val == 2, "value should be 2");

  htbl_free(htbl);
  return test_err;
}

int
test_insert_update()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // test
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expected 0 entries in table");

  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expected one entry in table (1)");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be assigned to 1");

  // test
  // insert can also update an entry's value
  err = htbl_insert(htbl, "one", 10);
  test_err |= FLA_ASSERT(htbl->len == 1, "(still) expect one entry in table(2)");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 10, "value should be reassigned to 10");

  htbl_free(htbl);
  return test_err;
}

// test what happens when two entries whose (distinct) keys yield the same hash value
// -- expect the second hash to differ and thus both entries to be inserted
int
test_insert_hash_collision_handling()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  // The implementation uses FLA_HTBL_H1 when deriving a hash which is supplied
  // to the compression function, determining the entry's ideal position in the table.
  //
  // To combat collision cases such as this, the implementation uses FLA_HTBL_H2 to
  // derive a second hash which is stored in entry->h2 and used when making equality checks
  FLA_ERR(FLA_HTBL_H1("c's") != FLA_HTBL_H1("ais"),
          "sanity check - this test may be invalidated due to a new hash function");

  err |= FLA_ASSERT(htbl->len == 0, "expected 0 entries in table");

  // test
  err |= htbl_insert(htbl, "ais", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expected one entry in table");
  e = _htbl_get_val(htbl, "ais");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  err = htbl_insert(htbl, "c's", 10);
  test_err |= FLA_ASSERT(htbl->len == 2, "expect two distinct entries in the hash table");
  e = _htbl_get_val(htbl, "c's");
  test_err |= FLA_ASSERT(e->val == 10, "value should be 10");

  test_err |= FLA_ASSERT(e->psl == 1,
                         "expect second element to have psl==1 as it is placed in the adjacent slot\n");

  htbl_free(htbl);
  return test_err;
}

int
test_insert_wrap_around()
{
  // test that inserts work even when we have to wrap around the table edge
  //fprintf(stdout, "MISSING TEST\n");
  return 0;
}

int
test_insert_full_table()
{
  // test inserting into full table, check for error
  //fprintf(stdout, "MISSING TEST\n");
  return 0;
}

int
test_lookup_single()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expect 0 entries in table");

  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expect one entry in table");

  // test
  e = htbl_lookup(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  htbl_free(htbl);
  return test_err;
}

int
test_lookup_multiple()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expect 0 entries in table");

  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expect one entry in table");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  err |= htbl_insert(htbl, "two", 2);
  test_err |= FLA_ASSERT(htbl->len == 2, "expect two entries in table");
  e = _htbl_get_val(htbl, "two");
  test_err |= FLA_ASSERT(e->val == 2, "value should be 2");

  // test
  e = htbl_lookup(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  e = htbl_lookup(htbl, "two");
  test_err |= FLA_ASSERT(e->val == 2, "value should be 2");
  htbl_free(htbl);
  return test_err;
}

int
test_lookup_non_existing()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expected 0 entries in table");

  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expected one entry in table (1)");

  // test
  e = htbl_lookup(htbl, "two");
  test_err |= FLA_ASSERT(e == NULL, "did not expect to get an entry");

  htbl_free(htbl);
  return test_err;
}

// TODO: lookup collision (?)

int
test_remove()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expect 0 entries in table");

  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expect one entry in table");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  err |= htbl_insert(htbl, "two", 2);
  test_err |= FLA_ASSERT(htbl->len == 2, "expect two entries in table");
  e = _htbl_get_val(htbl, "two");
  test_err |= FLA_ASSERT(e->val == 2, "value should be 2");

  // test
  htbl_remove(htbl, "one");
  test_err |= FLA_ASSERT(_htbl_len(htbl) == 1, "expect one entry left\n");
  test_err |= FLA_ASSERT(htbl->len == 1, "len variable not updated after remove");

  htbl_remove(htbl, "two");
  test_err |= FLA_ASSERT(_htbl_len(htbl) == 0, "expect 0 entries\n");
  test_err |= FLA_ASSERT(htbl->len == 0, "len variable not updated after remove");

  htbl_free(htbl);
  return test_err;
}

int
test_remove_wrap_around()
{
  // test remove works even when backshifting will wrap around table edge
  //fprintf(stdout, "MISSING TEST\n");
  return 0;
}

int
test_remove_non_existing()
{
  struct fla_htbl *htbl;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expect 0 entries in table");

  // test
  htbl_remove(htbl, "one");

  htbl_remove(htbl, "two");

  htbl_free(htbl);
  return test_err;
}

int
test_remove_idempotent()
{
  struct fla_htbl *htbl;
  struct fla_htbl_entry *e;
  int err = 0;
  int test_err = 0;

  // setup
  err = htbl_new(17, &htbl);
  if (FLA_ERR(err, HTBL_NEW_ERR))
    return -1;

  err |= FLA_ASSERT(htbl->len == 0, "expect 0 entries in table");

  err |= htbl_insert(htbl, "one", 1);
  test_err |= FLA_ASSERT(htbl->len == 1, "expect one entry in table");
  e = _htbl_get_val(htbl, "one");
  test_err |= FLA_ASSERT(e->val == 1, "value should be 1");

  err |= htbl_insert(htbl, "two", 2);
  test_err |= FLA_ASSERT(htbl->len == 2, "expect two entries in table");
  e = _htbl_get_val(htbl, "two");
  test_err |= FLA_ASSERT(e->val == 2, "value should be 2");

  // test
  htbl_remove(htbl, "two");
  test_err |= FLA_ASSERT(_htbl_len(htbl) == 1, "expect one entry left\n");
  test_err |= FLA_ASSERT(htbl->len == 1, "len variable not updated after remove");

  htbl_remove(htbl, "two");
  test_err |= FLA_ASSERT(_htbl_len(htbl) == 1, "expect one entry left (still)\n");

  htbl_free(htbl);
  return test_err;
}

int
run_test(char *test_label, int (*test_fn)())
{
  FLA_VBS_PRINTF("\n--- %s ---\n", test_label);
  return test_fn();
}

#define RUN_TEST(fn) run_test(#fn, fn);

int
main(int argc, char **argv)
{
  int err = 0;
  err |= RUN_TEST(test_initialization);

  err |= RUN_TEST(test_insert_one);
  err |= RUN_TEST(test_insert_multiple);
  err |= RUN_TEST(test_insert_update);
  err |= RUN_TEST(test_insert_hash_collision_handling);
  err |= RUN_TEST(test_insert_wrap_around);
  err |= RUN_TEST(test_insert_full_table);

  err |= RUN_TEST(test_lookup_single);
  err |= RUN_TEST(test_lookup_multiple);
  err |= RUN_TEST(test_lookup_non_existing);

  err |= RUN_TEST(test_remove);
  err |= RUN_TEST(test_remove_wrap_around);
  err |= RUN_TEST(test_remove_non_existing);
  err |= RUN_TEST(test_remove_idempotent);
  return err;
}
