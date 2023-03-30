#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "libflexalloc.h"
#include "libxnvme.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"

// We write in maximum 1G steps
#define WRITE_BUF_SIZE 1*1024*1024*1024
#define FILL_POOL_NAME "filler"

static void
usage()
{
    fprintf(stderr, "Usage: flexalloc_print DEV [MD_DEV] PERCENT\n"
                    "   DEV     The defice\n"
                    "   MD_DEV  The metadata device\n"
                    "   PERCENT The percentage that you want to fill\n"
                    "           NUM%%");
}

static int
fill_up_object(struct flexalloc * fs, struct fla_pool * pool_handle,
    struct fla_object * obj, void * write_buf)
{
  int ret = 0;
  uint64_t obj_nbytes = fla_object_size_nbytes(fs, pool_handle);


  size_t write_nbytes = fla_min(obj_nbytes, WRITE_BUF_SIZE);
  ret = fla_object_write(fs, pool_handle, obj, write_buf, 0, write_nbytes);
  if (FLA_ERR(ret, "fla_object_write()"))
    goto exit;

exit:
  return ret;
}

static int
fill(char const * dev, char const * md_dev, long int percent)
{
  int ret;
  struct fla_open_opts open_opts = {0};
  struct fla_pool_create_arg pool_arg = {0};

  struct xnvme_opts x_opts = xnvme_opts_default();
  struct flexalloc *fs;
  struct fla_pool * fill_pool;
  void *write_buf;

  open_opts.dev_uri = dev;
  open_opts.md_dev_uri = md_dev;
  open_opts.opts = &x_opts;
  open_opts.opts->async = "io_uring_cmd";

  ret = fla_open(&open_opts, &fs);
  if (FLA_ERR(ret, "fla_open()"))
    goto exit;

  // Calculate bytes to write
  uint64_t w_nlb = fs->geo.nlb / 100 * percent;

  // We will allcoate write buffer
  write_buf = fla_buf_alloc(fs, WRITE_BUF_SIZE);
  if ((ret = FLA_ERR(write_buf == NULL, "fla_buf_alloc()")))
    goto exit;

  // Create/Open filler pool
  ret = fla_pool_open(fs, FILL_POOL_NAME, &fill_pool);
  if (ret == -1)
  { // create the pool
    pool_arg.name = FILL_POOL_NAME;
    pool_arg.name_len = strlen(FILL_POOL_NAME);
    // -2 so we can fit the slab metadata
    pool_arg.obj_nlb = fs->geo.slab_nlb - 2;

    ret = fla_pool_create(fs, &pool_arg, &fill_pool);
    if (FLA_ERR(ret, "fla_pool_create()"))
      goto free_buf;
  }
  if (FLA_ERR(ret, "fla_pool_open()"))
    goto free_buf;

  struct fla_pool_entry * fill_pool_entry = fs->pools.entries + fill_pool->ndx;
  uint32_t nobj_to_write, nobj_to_write_ = w_nlb / fill_pool_entry->obj_nlb; //always round down
  for (nobj_to_write = nobj_to_write_; nobj_to_write > 0; --nobj_to_write)
  {
    struct fla_object * obj;
    ret = fla_object_create(fs, fill_pool, obj);
    if (FLA_ERR(ret, "fla_object_create()"))
      goto free_buf;

    ret = fill_up_object(fs, fill_pool, obj, write_buf);
    if (FLA_ERR(ret, "fill_up_object()"))
      goto free_buf;
  }

  fprintf(stderr, "Wrote %"PRIu32" objects out of %"PRIu32"\n",
      nobj_to_write_ - nobj_to_write, nobj_to_write_);

  fla_print_fs(fs);

  ret = fla_close(fs);
  if (FLA_ERR(ret, "fla_close()"))
    return ret;

free_buf:
  fla_buf_free(fs, write_buf);

exit:
  return ret;
}

int
main(int argc, char **argv)
{
  char *dev = NULL, *md_dev = NULL;
  int ret, percent_offset = 2;

  if((ret = (argc < 2 || argc > 4)))
  {
    usage();
    goto exit;
  }

  dev = argv[1];
  if (argc == 4)
  {
    md_dev = argv[2];
    percent_offset = 3;
  }

  char * endptr;
  long int percent;
  percent = strtol(argv[3], &endptr, 10);
  if ((ret = (*endptr != '%')))
  {
    fprintf(stderr, "PERCENT must be follwed by a '%%'");
    goto exit;
  }

  ret = fill(dev, md_dev, percent);

exit:
  return ret;
}
