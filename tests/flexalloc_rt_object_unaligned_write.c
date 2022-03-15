#include <asm-generic/errno-base.h>
#include <libxnvme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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

struct fs_test_vals
{
  char * pool_handle_name;
  struct flexalloc * fs;
  struct fla_pool * pool_handle;
  struct fla_object object_handle;
};

struct unaligned_test_vals
{
  char * orig_msg;
  size_t lhs_msg_len, rhs_msg_len, mid_msg_len, mid_start;
};

int test_unaligned_write(struct fs_test_vals const * fs_tv,
                         struct unaligned_test_vals const * u_tv);
int test_unaligned_writes(struct fs_test_vals const * fs_tv);

int
main(int argc, char **argv)
{
  int err, ret;
  struct fla_ut_dev dev;
  struct test_vals test_vals
      = {.blk_num = 40000, .slab_nlb = 4000, .npools = 1, .obj_nlb = 10};
  struct fs_test_vals fs_vals;

  fs_vals.pool_handle_name = "mypool";

  srand(getpid());

  err = fla_ut_dev_init(test_vals.blk_num, &dev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
  {
    goto exit;
  }

  // Unaligned writes are not compatible with ZNS
  if (dev._is_zns)
  {
    goto exit;
  }

  err = fla_ut_fs_create(test_vals.slab_nlb, test_vals.npools, &dev, &fs_vals.fs);
  if (FLA_ERR(err, "fla_ut_fs_create()"))
  {
    goto teardown_ut_dev;
  }

  err = fla_pool_create(fs_vals.fs, fs_vals.pool_handle_name, strlen(fs_vals.pool_handle_name),
                        test_vals.obj_nlb, &fs_vals.pool_handle);
  if(FLA_ERR(err, "fla_pool_create()"))
    goto teardown_ut_fs;

  err = fla_object_create(fs_vals.fs, fs_vals.pool_handle, &fs_vals.object_handle);
  if(FLA_ERR(err, "fla_object_create()"))
    goto release_pool;

  err = test_unaligned_writes(&fs_vals);
  if(FLA_ERR(err, "test_unaligned_write()"))
    goto release_object;

release_object:
  ret = fla_object_destroy(fs_vals.fs, fs_vals.pool_handle, &fs_vals.object_handle);
  if(FLA_ERR(ret, "fla_object_destroy()"))
    err = ret;

release_pool:
  ret = fla_pool_destroy(fs_vals.fs, fs_vals.pool_handle);
  if(FLA_ERR(ret, "fla_pool_destroy()"))
    err = ret;

teardown_ut_fs:
  ret = fla_ut_fs_teardown(fs_vals.fs);
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
test_unaligned_write(struct fs_test_vals const * fs_tv, struct unaligned_test_vals const * u_tv)
{
  int err = 0;
  char * lhs_msg, * rhs_msg, * mid_msg, * w_buf, * a_buf;
  size_t orig_msg_len, aligned_msg_len;

  orig_msg_len = strlen(u_tv->orig_msg);
  lhs_msg = u_tv->orig_msg;
  rhs_msg = u_tv->orig_msg + u_tv->lhs_msg_len;
  mid_msg = u_tv->orig_msg + u_tv->mid_start;

  aligned_msg_len = FLA_CEIL_DIV(orig_msg_len,
                                 fs_tv->fs->dev.lb_nbytes) * fs_tv->fs->dev.lb_nbytes;
  a_buf = fla_buf_alloc(fs_tv->fs, aligned_msg_len);
  if((err = FLA_ERR(!a_buf, "fla_buf_alloc()")))
  {
    err = -ENOENT;
    goto exit;
  }
  memset(a_buf, '!', fs_tv->fs->dev.lb_nbytes);
  err = fla_object_write(fs_tv->fs, fs_tv->pool_handle, &fs_tv->object_handle, a_buf,
                         aligned_msg_len - fs_tv->fs->dev.lb_nbytes, fs_tv->fs->dev.lb_nbytes);

  if(FLA_ERR(err, "fla_object_write()"))
    goto free_a_buf;

  w_buf = malloc(orig_msg_len);
  if((err = FLA_ERR(!w_buf, "malloc()")))
    goto free_a_buf;

  memcpy(w_buf, lhs_msg, u_tv->lhs_msg_len);
  err = fla_object_unaligned_write(fs_tv->fs, fs_tv->pool_handle, &fs_tv->object_handle, w_buf,
                                   0, u_tv->lhs_msg_len);
  if(FLA_ERR(err, "fla_object_unaligned_write()"))
    goto free_w_buf;

  memcpy(w_buf, rhs_msg, u_tv->rhs_msg_len);
  err = fla_object_unaligned_write(fs_tv->fs, fs_tv->pool_handle, &fs_tv->object_handle, w_buf,
                                   u_tv->lhs_msg_len, u_tv->rhs_msg_len);
  if(FLA_ERR(err, "fla_object_unaligned_write()"))
    goto free_w_buf;

  memcpy(w_buf, mid_msg, u_tv->mid_msg_len);
  err = fla_object_unaligned_write(fs_tv->fs, fs_tv->pool_handle, &fs_tv->object_handle, w_buf,
                                   u_tv->mid_start, u_tv->mid_msg_len);
  if(FLA_ERR(err, "fla_object_unaligned_write()"))
    goto free_w_buf;

  err = fla_object_read(fs_tv->fs, fs_tv->pool_handle, &fs_tv->object_handle, a_buf, 0,
                        aligned_msg_len);
  if(FLA_ERR(err, "fla_obj_read()"))
    goto free_w_buf;

  err = memcmp(a_buf, u_tv->orig_msg, orig_msg_len);
  if(FLA_ERR(err, "memcmp() - failed to read back the written value"))
    goto free_w_buf;

  if(aligned_msg_len > orig_msg_len)
  {
    if((err = FLA_ERR(*(a_buf + aligned_msg_len - 1) != '!', "Failed to maintain existing data")))
      goto free_w_buf;
  }


free_w_buf:
  free(w_buf);

free_a_buf:
  free(a_buf);

exit:
  return err;
}

int
test_unaligned_writes(struct fs_test_vals const * fs_tv)
{
  int err = 0;
  unsigned int test_lengths[2] = {12, 600}, test_length;
  struct unaligned_test_vals u_tv;

  u_tv.orig_msg = malloc(test_lengths[0]);
  if((err = FLA_ERR(!u_tv.orig_msg, "malloc()")))
    goto exit;

  for(int i = 0; i < 2 ; ++i)
  {
    test_length = test_lengths[i];

    u_tv.orig_msg = realloc(u_tv.orig_msg, test_length);
    if((err = FLA_ERR(!u_tv.orig_msg, "realloc()")))
      goto free_orig_msg;

    fla_t_fill_buf_random(u_tv.orig_msg, test_length - 1);
    u_tv.lhs_msg_len = test_length / 2;
    u_tv.rhs_msg_len = test_length - u_tv.lhs_msg_len;
    u_tv.mid_msg_len = u_tv.rhs_msg_len;
    u_tv.mid_start = u_tv.mid_msg_len / 2;

    err = test_unaligned_write(fs_tv, &u_tv);
    if(FLA_ERR(err, "test_unaligned_write()"))
      goto free_orig_msg;
  }

free_orig_msg:
  free(u_tv.orig_msg);

exit:
  return err;
}


