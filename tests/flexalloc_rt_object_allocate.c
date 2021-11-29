// Copyright (C) Jesper Devantier <j.granados@samsung.com>

#include <stdint.h>
#include "tests/flexalloc_tests_common.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"
#include "libflexalloc.h"

struct test_vals
{
  uint64_t blk_num;
  uint32_t npools;
  uint32_t slab_nlb;
  uint32_t obj_nlb;
  uint32_t dev_nobj;
};

static int test_objects(struct test_vals * test_vals);
#define FLA_UT_OBJECT_NUMBER_OF_TESTS 2

int
main(int argc, char** argv)
{

  int err = 0;
  struct test_vals test_vals [FLA_UT_OBJECT_NUMBER_OF_TESTS] =
  {
    {.blk_num = 40000, .npools = 2, .slab_nlb = 4000, .obj_nlb = 1, .dev_nobj = 2}
    , {.blk_num = 40000, .npools = 1, .slab_nlb = 4000, .obj_nlb = 5, .dev_nobj = 6}
  };

  for(int i = 0 ; i < FLA_UT_OBJECT_NUMBER_OF_TESTS ; ++i)
  {
    err = test_objects(&test_vals[i]);
    if(FLA_ERR(err, "test_slabs()"))
      goto exit;
  }

exit:
  return err;
}

static int
test_objects(struct test_vals * test_vals)
{
  int err, ret;
  struct fla_ut_dev dev;
  struct flexalloc *fs;
  char * pool_name = "pool1";
  struct fla_pool *pool_handle;
  struct fla_object *objs;

  objs = malloc(sizeof(struct fla_object) * test_vals->dev_nobj);
  if((err = FLA_ERR(!objs, "malloc()")))
    goto exit;

  err = fla_ut_dev_init(test_vals->blk_num, &dev);
  if (FLA_ERR(err, "fla_ut_dev_init"))
    goto free_objs;

  if (dev._is_zns)
  {
    test_vals->slab_nlb = dev.nsect_zn;
    test_vals->obj_nlb = dev.nsect_zn;
    test_vals->dev_nobj = 1;
  }

  err = fla_ut_fs_create(test_vals->slab_nlb, test_vals->npools, &dev, &fs);
  if (FLA_ERR(err, "fla_ut_fs_create()"))
    goto teardown_ut_dev;

  err = fla_pool_create(fs, pool_name, strlen(pool_name), test_vals->obj_nlb, &pool_handle);
  if(FLA_ERR(err, "fla_pool_create()"))
    goto teardown_ut_fs;

  // Allocate all possible objects
  for(size_t objs_offset = 0 ; objs_offset < test_vals->dev_nobj; ++objs_offset)
  {
    err = fla_object_create(fs, pool_handle, &objs[objs_offset]);
    if(FLA_ERR(err, "fla_object_create()"))
      goto release_pool;
  }

  // Make sure we cannot allocate more
  /*err = fla_object_create(fs, pool_handle, &obj);
  if(FLA_ASSERTF(err != 0, "Allocated past the max value %d\n", test_vals->dev_nobj))
  {
    goto release_pool;
  }*/

  for(size_t objs_offset = 0 ; objs_offset < test_vals->dev_nobj; ++ objs_offset)
  {
    err = fla_object_destroy(fs, pool_handle, &objs[objs_offset]);
    if(FLA_ERR(err, "fla_object_destroy()"))
      goto release_pool;
  }

release_pool:
  ret = fla_pool_destroy(fs, pool_handle);
  if(FLA_ERR(ret, "fla_pool_destroy()"))
    err = ret;

teardown_ut_fs:
  ret = fla_ut_fs_teardown(fs);
  if (FLA_ERR(ret, "fla_ut_fs_teardown()"))
    err = ret;

teardown_ut_dev:
  ret = fla_ut_dev_teardown(&dev);
  if (FLA_ERR(ret, "fla_ut_dev_teardown()"))
    err = ret;

free_objs:
  free(objs);

exit:
  return err;
}
