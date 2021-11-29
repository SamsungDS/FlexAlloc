// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
#include "tests/flexalloc_tests_common.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"
#include "libflexalloc.h"
#include <stdint.h>

struct test_vals
{
  /* Initial variables */
  uint64_t blk_num;
  uint32_t npools;
  uint32_t slab_nlb;
  char *dev_path;
  char *md_dev_path;
};

int test_mkfs(struct test_vals * test_vals);

int
main(int argc, char ** argv)
{
  int err = 0;
  struct test_vals test_vals [4] =
  {
    {.blk_num = 40000, .npools = 2, .slab_nlb = 4000}
    , {.blk_num = 80000, .npools = 2, .slab_nlb = 4000},
  };

  for(int i = 0 ; i < 2 ; ++i)
  {

    err = test_mkfs(&test_vals[i]);
    if(FLA_ERR(err, "test_mkfs()"))
      goto exit;
  }

exit:
  return err;
}

int
test_mkfs(struct test_vals * test_vals)
{
  int err, ret = 0;
  struct fla_ut_dev dev = {0};
  struct flexalloc *fs;

  err = fla_ut_dev_init(test_vals->blk_num, &dev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
    goto exit;

  if (dev._is_zns)
  {
    test_vals->blk_num = dev.nblocks;
    test_vals->slab_nlb = dev.nsect_zn;
  }

  err = fla_ut_fs_create(test_vals->slab_nlb, test_vals->npools, &dev, &fs);
  if(FLA_ERR(err, "fla_ut_fs_create()"))
    goto teardown_ut_dev;


  err |= FLA_ASSERTF(fs->super->npools == test_vals->npools,
                     "Unexpected number of pools (%"PRIu32" == %"PRIu32")", fs->super->npools, test_vals->npools);
  err |= FLA_ASSERTF(fs->super->slab_nlb == test_vals->slab_nlb,
                     "Unexpected size of slab (%"PRIu64" == %"PRIu32")", fs->super->slab_nlb
                     ,test_vals->slab_nlb);
  err |= FLA_ASSERTF(fs->geo.nlb == dev.nblocks,
                     "Unexpected number of lbas, (%"PRIu64" == %"PRIu64")", fs->geo.nlb, dev.nblocks);
  err |= FLA_ASSERTF(fs->geo.lb_nbytes == dev.lb_nbytes,
                     "Unexpected lba width, (%"PRIu32" == %"PRIu64")", fs->geo.lb_nbytes, dev.lb_nbytes);

  ret = fla_ut_fs_teardown(fs);
  if(FLA_ERR(ret, "fla_ut_fs_teardown()"))
    err = ret;

teardown_ut_dev:
  ret = fla_ut_dev_teardown(&dev);
  if (FLA_ERR(ret, "fla_ut_dev_teardown()"))
    err = ret;

exit:
  return err;
}
