#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "src/libflexalloc.h"
#include "src/flexalloc.h"
#include "src/flexalloc_util.h"
#include "tests/flexalloc_tests_common.h"

struct test_vals
{
  uint32_t obj_nstrp;
  uint32_t strp_nlbs;
  uint32_t npools;
  uint32_t strp_nobj;

  uint32_t obj_nlb;
  uint32_t slab_nlb;
  uint32_t blk_nlbs;

  uint32_t xfer_snlb;
  uint32_t xfer_nlbs;
  uint32_t xfer_snstrps;
  uint32_t xfer_nstrps;
};

#define NUM_TESTS 1
struct test_vals tests[] =
{
  // Simple write all.
  {
    .obj_nstrp = 3072, .strp_nlbs = 2, .npools = 1, .strp_nobj = 2,
    .obj_nlb = 0, .blk_nlbs = 0, .slab_nlb = 0,
    .xfer_snlb = 0, .xfer_nlbs = 0,
    .xfer_snstrps = 0, .xfer_nstrps = 4,
  },

  // start from second chunk and wrap around
  {
    .obj_nstrp = 3072, .strp_nlbs = 4, .npools = 1, .strp_nobj = 4,
    .obj_nlb = 0, .blk_nlbs = 0, .slab_nlb = 0,
    .xfer_snlb = 0, .xfer_nlbs = 0,
    .xfer_snstrps = 1, .xfer_nstrps = 1
  },

  // several transfers in each object
  {
    .obj_nstrp = 3072, .strp_nlbs = 4, .npools = 1, .strp_nobj = 4,
    .obj_nlb = 0, .blk_nlbs = 0, .slab_nlb = 0,
    .xfer_snlb = 4, .xfer_nlbs = 48,
    .xfer_snstrps = 0, .xfer_nstrps = 3072 * 2
  },
};

bool
should_write_fail(struct fla_ut_dev * dev, struct test_vals * test_vals)
{
  if(dev->_is_zns)
  {
    if(test_vals->xfer_snstrps > test_vals->obj_nstrp)
      return true;
  }
  return false;
}


int
test_strp(struct test_vals test_vals)
{
  int err = 0, ret;
  char * pool_handle_name, *write_buf, *read_buf;
  size_t buf_len;
  uint64_t xfer_offset;
  struct fla_ut_dev dev;
  struct flexalloc *fs = NULL;
  struct fla_pool *pool_handle;
  struct fla_object obj;
  struct fla_open_opts open_opts = {0};

  pool_handle_name = "mypool";

  if(FLA_ERR(test_vals.xfer_nstrps < 1, "Test needs to transfer more than zero lbs"))
    goto exit;

  test_vals.obj_nlb = test_vals.obj_nstrp * test_vals.strp_nlbs;
  test_vals.slab_nlb = test_vals.obj_nlb * test_vals.strp_nobj * 4;
  test_vals.blk_nlbs = test_vals.slab_nlb * 10;

  err = fla_ut_dev_init(test_vals.blk_nlbs, &dev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
    goto exit;

  if(test_vals.blk_nlbs != dev.nblocks)
  {
    // "Real" device
    if(!dev._is_zns)
      goto teardown_ut_dev; // ignore non ZNS for now.

    test_vals.obj_nlb = dev.nsect_zn;
    if (test_vals.obj_nlb % test_vals.obj_nstrp > 0)
      goto teardown_ut_dev; // zone must be a multiple obj_nstrp
    test_vals.strp_nlbs = test_vals.obj_nlb / test_vals.obj_nstrp;

    test_vals.slab_nlb = test_vals.obj_nlb * test_vals.strp_nobj * 4;
    test_vals.blk_nlbs = dev.nblocks;
  }

  test_vals.xfer_snlb = test_vals.xfer_snstrps * test_vals.strp_nlbs;
  test_vals.xfer_nlbs = test_vals.xfer_nstrps * test_vals.strp_nlbs;

  err = fla_ut_fs_create(test_vals.slab_nlb, test_vals.npools, &dev, &fs);
  if (FLA_ERR(err, "fla_ut_fs_create()"))
  {
    goto teardown_ut_dev;
  }

  buf_len = test_vals.xfer_nlbs * dev.lb_nbytes;
  xfer_offset = test_vals.xfer_snlb * dev.lb_nbytes;

  err = fla_pool_create(fs, pool_handle_name, strlen(pool_handle_name), test_vals.obj_nlb,
                        &pool_handle);
  if(FLA_ERR(err, "fla_pool_create()"))
    goto teardown_ut_fs;

  err = fla_pool_set_strp(fs, pool_handle, test_vals.strp_nobj,
                          dev.lb_nbytes * test_vals.strp_nlbs);
  if (FLA_ERR(err, "fla_pool_set_strp()"))
    goto release_pool;

  err = fla_object_create(fs, pool_handle, &obj);
  if(FLA_ERR(err, "fla_object_create()"))
    goto release_pool;

  write_buf = fla_buf_alloc(fs, buf_len);
  if((err = FLA_ERR(!write_buf, "fla_buf_alloc(): buf_len :%"PRIu64"")))
    goto release_object;

  fla_t_fill_buf_random(write_buf, buf_len);
  write_buf[buf_len] = '\0';

  err = fla_object_write(fs, pool_handle, &obj, write_buf, xfer_offset, buf_len);
  if(should_write_fail(&dev, &test_vals) && err == 0)
  {
    FLA_ERR(1, "fla_object_write(): Expected write failure, got success");
    goto free_write_buffer;
  }

  if(FLA_ERR(err, "fla_object_write()"))
    goto free_write_buffer;

  err = fla_close(fs);
  if(FLA_ERR(err, "fla_close()"))
    goto free_write_buffer;

  open_opts.dev_uri = dev._dev_uri;
  open_opts.md_dev_uri = dev._md_dev_uri;
  err = fla_open(&open_opts, &fs);
  if(FLA_ERR(err, "fla_open()"))
    goto free_write_buffer;

  fla_pool_close(fs, pool_handle);
  err = fla_pool_open(fs, pool_handle_name, &pool_handle);
  if(FLA_ERR(err, "fla_pool_open()"))
    goto free_write_buffer;

  err = fla_object_open(fs, pool_handle, &obj);
  if(FLA_ERR(err, "fla_object_open()"))
    goto free_write_buffer;

  read_buf = fla_buf_alloc(fs, buf_len);
  if((err = FLA_ERR(!read_buf, "fla_buf_alloc()")))
    goto free_write_buffer;
  memset(read_buf, 0, buf_len);
  read_buf[buf_len] = '\0';

  err = fla_object_read(fs, pool_handle, &obj, read_buf, xfer_offset, buf_len);
  if(FLA_ERR(err, "fla_obj_read()"))
    goto free_read_buffer;

  // Compare that the string (and terminating NULL) was read back
  err = memcmp(write_buf,  read_buf, buf_len + 1);

  if(FLA_ERR(err, "Unexpected value for memcmp"))
    goto free_write_buffer;

  // Free the object, which should reset a zone
  ret = fla_object_destroy(fs, pool_handle, &obj);
  if(FLA_ERR(ret, "fla_object_destroy()"))
    err = ret;

  // Allocate a new object
  err = fla_object_create(fs, pool_handle, &obj);
  if(FLA_ERR(err, "fla_object_create()"))
    goto release_pool;

  // Will fail on zns without object reset
  err = fla_object_write(fs, pool_handle, &obj, write_buf, 0, buf_len);
  if(FLA_ERR(err, "fla_object_write()"))
    goto free_write_buffer;

free_read_buffer:
  fla_buf_free(fs, read_buf);

free_write_buffer:
  fla_buf_free(fs, write_buf);

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
  {
    err = ret;
  }

teardown_ut_dev:
  ret = fla_ut_dev_teardown(&dev);
  if (FLA_ERR(ret, "fla_ut_dev_teardown()"))
  {
    err = ret;
  }

exit:
  return err;
}

int
main(int argc, char **argv)
{
  int err = 0;
  // We seed the rand with pid so it produces different values always
  srand(getpid());

  for (int i = 0 ; i < NUM_TESTS ; ++i)
    err |= test_strp(tests[i]);

  return err;
}
