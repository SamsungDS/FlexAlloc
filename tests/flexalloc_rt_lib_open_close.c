#include <libxnvme.h>
#include <libxnvme_dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libflexalloc.h"
#include "flexalloc_mm.h"
#include "flexalloc_util.h"
#include "flexalloc_xnvme_env.h"
#include "tests/flexalloc_tests_common.h"

int
main(int argc, char **argv)
{
  int err, ret;
  char * pool_handle_name, * write_msg, *write_buf, *read_buf;
  size_t write_msg_len, buf_len;
  struct flexalloc *fs = NULL;
  struct fla_pool *pool_handle;
  struct fla_object obj;
  struct fla_ut_dev tdev = {0};

  err = fla_ut_dev_init(2000, &tdev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
  {

    goto exit;
  }

  if (!tdev._is_zns)
    err = fla_ut_fs_create(500, 1, &tdev, &fs);
  else
    err = fla_ut_fs_create(tdev.nsect_zn, 1, &tdev, &fs);

  if (FLA_ERR(err, "fla_ut_fs_create()"))
  {
    goto teardown_ut_dev;
  }

  pool_handle_name = "mypool";
  struct fla_pool_create_arg pool_arg =
  {
    .flags = 0,
    .name = pool_handle_name,
    .name_len = strlen(pool_handle_name),
    .obj_nlb = tdev.nsect_zn
  };

  if (!tdev._is_zns)
    pool_arg.obj_nlb = 2;

  err = fla_pool_create(fs, &pool_arg, &pool_handle);

  if(FLA_ERR(err, "fla_pool_create()"))
  {
    goto teardown_ut_fs;
  }

  err = fla_object_create(fs, pool_handle, &obj);
  if(FLA_ERR(err, "fla_object_create()"))
  {
    goto release_pool;
  }

  write_msg = "hello, world";
  write_msg_len = strlen(write_msg);
  buf_len = FLA_CEIL_DIV(write_msg_len, tdev.lb_nbytes) * tdev.lb_nbytes;

  read_buf = fla_buf_alloc(fs, buf_len);
  if((err = FLA_ERR(!read_buf, "fla_buf_alloc()")))
  {
    goto release_object;
  }

  write_buf = fla_buf_alloc(fs, buf_len);
  if((err = FLA_ERR(!write_buf, "fla_buf_alloc()")))
  {
    goto free_read_buffer;
  }
  memcpy(write_buf, write_msg, write_msg_len);
  write_buf[write_msg_len] = '\0';

  err = fla_object_write(fs, pool_handle, &obj, write_buf, 0, buf_len);
  if(FLA_ERR(err, "fla_object_write()"))
  {
    goto free_write_buffer;
  }

  err = fla_object_read(fs, pool_handle, &obj, read_buf, 0, buf_len);
  if(FLA_ERR(err, "fla_obj_read()"))
  {
    goto free_write_buffer;
  }

  // compare that the string (and terminating NULL) was read back
  err = memcmp(write_buf,  read_buf, write_msg_len + 1);
  if(FLA_ERR(err, "memcmp() - failed to read back the written value"))
  {
    goto free_write_buffer;
  }

free_write_buffer:
  fla_buf_free(fs, write_buf);

free_read_buffer:
  fla_buf_free(fs, read_buf);

release_object:
  ret = fla_object_destroy(fs, pool_handle, &obj);
  if(FLA_ERR(ret, "fla_object_destroy()"))
  {
    err = ret;
  }

release_pool:
  ret = fla_pool_destroy(fs, pool_handle);
  if(FLA_ERR(ret, "fla_pool_destroy()"))
  {
    err = ret;
  }

teardown_ut_fs:
  ret = fla_ut_fs_teardown(fs);
  if (FLA_ERR(ret, "fla_ut_fs_teardown()"))
  {
    err = ret;
  }

teardown_ut_dev:
  ret = fla_ut_dev_teardown(&tdev);
  if (FLA_ERR(ret, "fla_ut_dev_teardown()"))
  {
    err = ret;
  }

exit:
  return err;
}
