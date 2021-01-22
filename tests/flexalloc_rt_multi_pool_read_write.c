#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/libflexalloc.h"
#include "src/flexalloc.h"
#include "src/flexalloc_util.h"
#include "tests/flexalloc_tests_common.h"

#define NUM_POOLS 2

struct test_vals
{
  uint64_t blk_size;
  uint64_t blk_num;
  uint32_t npools;
  uint32_t slab_nlb;
  uint32_t obj_nlb;
};

int
main(int argc, char **argv)
{
  int err = 0;
  char *pool_handle_names[NUM_POOLS] = {"mypool0", "mypool1"};
  char* write_msgs[NUM_POOLS] = {"hello, world pool0", "hello, world pool1"};
  char *write_bufs[NUM_POOLS]  = {NULL, NULL}, *read_buf = NULL;
  size_t write_msg_lens[NUM_POOLS] = {strlen(write_msgs[0]), strlen(write_msgs[1])};
  size_t buf_len;
  struct flexalloc *fs = NULL;
  struct fla_pool *pool_handle[NUM_POOLS] = {NULL, NULL};
  struct fla_object obj[NUM_POOLS] = {0};
  struct test_vals t_val = {0};
  struct fla_ut_dev tdev = {0};

  err = fla_ut_dev_init(t_val.blk_num, &tdev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
    goto exit;

  // If we are not using a ZNS device we aren't really interested in this test.
  // This test is to demonstrate that one zone will be left opened by flexalloc
  if (!tdev._is_zns)
  {
    goto teardown_ut_dev;
  }

  t_val.blk_size = tdev.lb_nbytes;
  t_val.blk_num = tdev.nblocks;
  t_val.slab_nlb = tdev.nsect_zn;
  t_val.obj_nlb = tdev.nsect_zn;
  t_val.npools = NUM_POOLS;

  err = fla_ut_fs_create(t_val.slab_nlb, t_val.npools, &tdev, &fs);
  if(FLA_ERR(err, "fla_ut_fs_create()"))
    goto teardown_ut_dev;

  for (int pn = 0; pn < NUM_POOLS; pn++)
  {
    buf_len = FLA_CEIL_DIV(write_msg_lens[pn], t_val.blk_size) * t_val.blk_size;
    err = fla_pool_create(fs, pool_handle_names[pn], strlen(pool_handle_names[pn]),
                          t_val.obj_nlb, &pool_handle[pn]);
    if(FLA_ERR(err, "fla_pool_create()"))
      goto teardown_ut_fs;

    err = fla_object_create(fs, pool_handle[pn], &obj[pn]);
    if(FLA_ERR(err, "fla_object_create()"))
      goto release_pool;

    write_bufs[pn] = fla_buf_alloc(fs, buf_len);
    if((err = FLA_ERR(!write_bufs[pn], "fla_buf_alloc()")))
      goto release_object;

    memset(write_bufs[pn], 0, buf_len);
    memcpy(write_bufs[pn], write_msgs[pn], write_msg_lens[pn]);
    write_bufs[pn][write_msg_lens[pn]] = '\0';

    err = fla_object_write(fs, pool_handle[pn], &obj[pn], write_bufs[pn], 0, buf_len);
    if(FLA_ERR(err, "fla_object_write()"))
      goto free_write_buffer;
  }

  err = fla_close(fs);
  if(FLA_ERR(err, "fla_close()"))
    goto free_write_buffer;

  err = fla_md_open(tdev._dev_uri, tdev._md_dev_uri, &fs);

  if(FLA_ERR(err, "fla_open()"))
    goto free_write_buffer;

  for (int pn = 0; pn < NUM_POOLS; pn++)
  {
    buf_len = FLA_CEIL_DIV(write_msg_lens[pn], t_val.blk_size) * t_val.blk_size;
    err = fla_pool_open(fs, pool_handle_names[pn], &pool_handle[pn]);
    if(FLA_ERR(err, "fla_pool_lookup()"))
      goto free_write_buffer;

    err = fla_object_open(fs, pool_handle[pn], &obj[pn]);
    if(FLA_ERR(err, "fla_object_open()"))
      goto free_write_buffer;

    if (!read_buf)
      read_buf = fla_buf_alloc(fs, buf_len);
    if((err = FLA_ERR(!read_buf, "fla_buf_alloc()")))
      goto free_write_buffer;

    memset(read_buf, 0, buf_len);
    err = fla_object_read(fs, pool_handle[pn], &obj[pn], read_buf, 0, buf_len);
    if(FLA_ERR(err, "fla_obj_read()"))
      goto free_read_buffer;

    // compare that the string (and terminating NULL) was read back
    err = memcmp(write_bufs[pn],  read_buf, write_msg_lens[pn] + 1);
    if(FLA_ERR(err, "memcmp() - failed to read back the written value"))
      goto free_write_buffer;
  }

free_read_buffer:
  fla_buf_free(fs, read_buf);

free_write_buffer:
  for (int pn = 0; pn < NUM_POOLS; pn++)
  {
    if (write_bufs[pn])
      fla_buf_free(fs, write_bufs[pn]);
  }

release_object:
  for (int pn = 0; pn < NUM_POOLS; pn++)
  {
    if ((obj[pn].slab_id || obj[pn].entry_ndx) && pn == 1)
    {
      err = fla_object_destroy(fs, pool_handle[pn], &obj[pn]);
      FLA_ERR(err, "fla_object_destroy()");
    }
  }

release_pool:
  for (int pn = 0; pn < NUM_POOLS; pn++)
  {
    if (pool_handle[pn] && pn == 1)
      fla_pool_close(fs, pool_handle[pn]);
  }

teardown_ut_fs:
  err = fla_ut_fs_teardown(fs);
  FLA_ERR(err, "fla_ut_fs_teardown()");

teardown_ut_dev:
  err = fla_ut_dev_teardown(&tdev);
  FLA_ERR(err, "fla_ut_dev_teardown()");

exit:
  return err;
}
