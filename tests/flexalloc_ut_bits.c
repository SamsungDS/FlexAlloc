// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>

#include <stdio.h>
#include "src/flexalloc_bits.h"
#include "src/flexalloc_util.h"
#include "flexalloc_tests_common.h"

int
test_ntz()
{
  int err = 0;
  err |= FLA_ASSERT(ntz(0) == 32, "expected ntz(0) == 32");

  err |= FLA_ASSERT(ntz(~0) == 0,  "expected ntz(1) == 0");

  err |= FLA_ASSERT(ntz(4) == 2, "expected ntz(4) == 2 (100 in binary)");

  err |= FLA_ASSERT(ntz(256) == 8, "expected ntz(256) == 8");

  return err;
}

#define ASSERT_SET_BITS(val, num_set)             \
  FLA_ASSERTF(count_set_bits(val) == num_set, "%u should have %u set bits", val, num_set)


int
test_count_set_bits()
{
  int err = 0;

  err |= ASSERT_SET_BITS(0, 0);
  err |= ASSERT_SET_BITS(~0, 32);
  err |= ASSERT_SET_BITS(7, 3);

  return err;
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

  err |= RUN_TEST(test_ntz);
  err |= RUN_TEST(test_count_set_bits);

  return err;
}
