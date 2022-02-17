// Copyright (C) 2022 Adam Manzanares <a.manzanares@samsung.com>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <libflexalloc.h>
#include <libxnvme.h>

#define POOL_NAME "TEST_POOL"
#define USAGE "./bw_tester fla_dev fla_md_dev num_rw obj_size_blks strp_objs strp_sz wrt_sz verify"

int r_w_obj(struct flexalloc *fs, struct fla_pool *pool, struct fla_object *obj,
            uint64_t num, uint64_t wrt_sz, bool write, bool verify)
{
  int ret;
  char *buf;
  struct xnvme_timer time;

  buf = fla_buf_alloc(fs, wrt_sz);
  if (!buf) {
    printf("Allocating object buffer fails\n");
    return -ENOMEM;
  }

  if (write)
  {
    for (uint64_t cur_byte = 0; cur_byte < wrt_sz; cur_byte++)
      buf[cur_byte] = (char)cur_byte;
  }

  xnvme_timer_start(&time);
  for (uint64_t cur = 0; cur < num; cur++) {
    if (write)
      ret = fla_object_write(fs, pool, obj, buf, cur * wrt_sz, wrt_sz);
    else
      ret = fla_object_read(fs, pool, obj, buf, cur * wrt_sz, wrt_sz);

    if (ret) {
      printf("Object write fails. Cur:%lu\n", cur);
      return ret;
    }

    if (!write && verify)
    {
      for (uint64_t cur_byte = 0; cur_byte < wrt_sz; cur_byte++)
      {
        if (buf[cur_byte] != (char)cur_byte)
        {
          printf("Data mismatch cur:%lu offset:%lu,expected:[%c],got:[%c]\n", cur, cur_byte,
                 (char)cur_byte, buf[cur_byte]);
          return -1;
        }
      }
    }
  }

  xnvme_timer_stop(&time);

  xnvme_timer_bw_pr(&time, "wall-clock", wrt_sz * num);
  return ret;
}

int main(int argc, char **argv)
{
  struct flexalloc *fs;
  struct fla_pool *pool;
  struct fla_object obj;
  int ret;
  char *dev, *md_dev;
  uint64_t num_writes, obj_size, strp_num, strp_sz, wrt_sz;
  bool verify;

  if (argc != 9) {
    printf("Usage:%s\n", USAGE);
    return -1;
  }

  dev = argv[1];
  md_dev = argv[2];
  num_writes = atoi(argv[3]);
  obj_size = atoi(argv[4]);
  strp_num = atoi(argv[5]);
  strp_sz = atoi(argv[6]);
  wrt_sz = atoi(argv[7]);
  verify = atoi(argv[8]);

  ret = fla_md_open(dev, md_dev, &fs);
  if (ret) {
    printf("Error on open\n");
    goto exit;
  }

  ret = fla_pool_create(fs, POOL_NAME, strlen(POOL_NAME), obj_size, &pool);
  if (ret) {
    printf("Error on pool create\n");
    goto close;
  }

  ret = fla_pool_set_strp(fs, pool, strp_num, strp_sz);
  if (ret) {
    printf("Error setting pool strp sz\n");
    goto close;
  }

  ret = fla_object_create(fs, pool, &obj);
  if (ret) {
    printf("Object create fails\n");
    return ret;
  }

  ret = r_w_obj(fs, pool, &obj, num_writes, wrt_sz, true, false);
  ret = r_w_obj(fs, pool, &obj, num_writes, wrt_sz, false, verify);

close:
  fla_close(fs);

exit:
	return ret;
}
