#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libflexalloc.h"
#include "flexalloc.h"
#include "flexalloc_util.h"
#include "tests/flexalloc_tests_common.h"
#include <inttypes.h>

struct test_vals
{
  uint64_t blk_num;
  uint32_t npools;
  uint32_t slab_nlb;
  uint32_t obj_nlb;
};

int
main(int argc, char **argv)
{
  int err, ret;
  char * pool_handle_name, *buf;
  size_t buf_len;
  struct fla_ut_dev dev;
  struct flexalloc *fs = NULL;
  struct fla_pool *pool_handle;
  struct fla_object obj;
  struct test_vals test_vals
      = {.blk_num = 40000, .slab_nlb = 4000, .npools = 1, .obj_nlb = 2};

  pool_handle_name = "mypool";

  err = fla_ut_dev_init(test_vals.blk_num, &dev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
    goto exit;

  if (dev._is_zns)
  {
    test_vals.slab_nlb = dev.nsect_zn;
    test_vals.obj_nlb = dev.nsect_zn;
  }

  err = fla_ut_fs_create(test_vals.slab_nlb, test_vals.npools, &dev, &fs);
  if (FLA_ERR(err, "fla_ut_fs_create()"))
    goto teardown_ut_dev;

  buf_len = FLA_CEIL_DIV(dev.lb_nbytes * (test_vals.obj_nlb + 1),
                         dev.lb_nbytes) * dev.lb_nbytes;

  err = fla_pool_create(fs, pool_handle_name, strlen(pool_handle_name), test_vals.obj_nlb,
                        &pool_handle);
  if(FLA_ERR(err, "fla_pool_create()"))
    goto teardown_ut_fs;

  err = fla_object_create(fs, pool_handle, &obj);
  if(FLA_ERR(err, "fla_object_create()"))
    goto release_pool;

  fprintf(stderr, "dev.lb_nbytes(%"PRIu64"), buf_len(%zu)\n", dev.lb_nbytes, buf_len);
  buf = fla_buf_alloc(fs, buf_len);
  if((err = FLA_ERR(!buf, "fla_buf_alloc()")))
    goto release_object;

  ret = fla_object_write(fs, pool_handle, &obj, buf, 0, buf_len);
  err |= FLA_ASSERT(ret != 0, "We need to fail when we write over the object limit");
  ret = fla_object_read(fs, pool_handle, &obj, buf, 0, buf_len);
  err |= FLA_ASSERT(ret != 0, "We need to fail when we read over the object limit");

  fla_buf_free(fs, buf);

release_object:
  ret = fla_object_destroy(fs, pool_handle, &obj);
  if(FLA_ERR(ret, "fla_object_destroy()"))
    err = ret;

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

exit:
  return err;
}
