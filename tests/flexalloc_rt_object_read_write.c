#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libflexalloc.h"
#include "flexalloc.h"
#include "flexalloc_util.h"
#include "tests/flexalloc_tests_common.h"

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
  char * pool_handle_name, * write_msg, *write_buf, *read_buf;
  size_t write_msg_len, buf_len;
  struct fla_ut_dev dev;
  struct flexalloc *fs = NULL;
  struct fla_pool *pool_handle;
  struct fla_object obj;
  struct fla_open_opts open_opts = {0};
  struct test_vals test_vals
      = {.blk_num = 40000, .slab_nlb = 4000, .npools = 1, .obj_nlb = 2};

  pool_handle_name = "mypool";
  write_msg = "hello, world";
  write_msg_len = strlen(write_msg);

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
  {
    goto teardown_ut_dev;
  }

  buf_len = FLA_CEIL_DIV(write_msg_len, dev.lb_nbytes) * dev.lb_nbytes;

  err = fla_pool_create(fs, pool_handle_name, strlen(pool_handle_name), test_vals.obj_nlb,
                        &pool_handle);
  if(FLA_ERR(err, "fla_pool_create()"))
    goto teardown_ut_fs;

  err = fla_object_create(fs, pool_handle, &obj);
  if(FLA_ERR(err, "fla_object_create()"))
    goto release_pool;

  write_buf = fla_buf_alloc(fs, buf_len);
  if((err = FLA_ERR(!write_buf, "fla_buf_alloc()")))
    goto release_object;

  memcpy(write_buf, write_msg, write_msg_len);
  write_buf[write_msg_len] = '\0';

  err = fla_object_write(fs, pool_handle, &obj, write_buf, 0, buf_len);
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

  err = fla_object_read(fs, pool_handle, &obj, read_buf, 0, buf_len);
  if(FLA_ERR(err, "fla_obj_read()"))
    goto free_read_buffer;

  // compare that the string (and terminating NULL) was read back
  err = memcmp(write_buf,  read_buf, buf_len + 1);
  if(FLA_ERR(err, "memcmp() - failed to read back the written value"))
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
