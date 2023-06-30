#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "libflexalloc.h"
#include "libxnvme.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"
#include "flexalloc_cs_zns.h"
#include "src/flexalloc_pool.h"

// We write in maximum 1G steps
#define WRITE_BUF_SIZE 1*1024*1024*1024
#define FILL_POOL_NAME "filler"

static void
usage()
{
    fprintf(stderr, "Usage: flexalloc_fill DEV [MD_DEV] PERCENT\n"
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
  size_t nbytes_left_to_write = obj_nbytes;
  size_t w_offset = 0;

  size_t curr_write_nbytes = fla_min(nbytes_left_to_write, WRITE_BUF_SIZE);
  do {
    ret = fla_object_write(fs, pool_handle, obj, write_buf, w_offset, curr_write_nbytes);
    if (FLA_ERR(ret, "fla_object_write()"))
      goto exit;
    nbytes_left_to_write -= curr_write_nbytes;
    w_offset += curr_write_nbytes;
    curr_write_nbytes = fla_min(nbytes_left_to_write, WRITE_BUF_SIZE);
  } while (curr_write_nbytes != 0);

  ret = fla_object_seal(fs, pool_handle, obj);
  if (FLA_ERR(ret, "fla_object_seal"))
    goto exit;

exit:
  return ret;
}

static uint32_t
calc_best_object_size( struct flexalloc *fs)
{
  if (fs->fla_cs.cs_t == FLA_CS_ZNS)
    return fs->fla_cs.fla_cs_zns->nzsect;
  else
    return fs->geo.slab_nlb;
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
  fprintf(stderr, "number of logical blocs to write %"PRIu64"\n", w_nlb);

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
    pool_arg.obj_nlb = calc_best_object_size(fs);
    pool_arg.strp_nobjs = 16;
    pool_arg.strp_nbytes = 32768;
    pool_arg.flags |= FLA_POOL_ENTRY_STRP;

    fprintf(stderr, "creating pool\n");
    ret = fla_pool_create(fs, &pool_arg, &fill_pool);
    if (FLA_ERR(ret, "fla_pool_create()"))
      goto free_buf;
  }
  if (FLA_ERR(ret, "fla_pool_open()"))
    goto free_buf;

  uint64_t strp_obj_nlb_internal
    = (fs->pools.entrie_funcs + fill_pool->ndx)->fla_pool_obj_size_nbytes(fs, fill_pool)
      /fs->geo.lb_nbytes;

  uint32_t nobj_to_write, nobj_to_write_ = w_nlb / strp_obj_nlb_internal; //always round down
  fprintf(stderr, "Number of objects to write %"PRIu32"\n", nobj_to_write_);

  for (nobj_to_write = nobj_to_write_; nobj_to_write > 0; --nobj_to_write)
  {
    struct fla_object obj = {0};
    ret = fla_object_create(fs, fill_pool, &obj);
    if (FLA_ERR(ret, "fla_object_create()"))
      goto free_buf;

    ret = fill_up_object(fs, fill_pool, &obj, write_buf);
    if (FLA_ERR(ret, "fill_up_object()"))
      goto free_buf;

    ret = fla_sync(fs);
    if (FLA_ERR(ret, "fla_sync"))
      goto free_buf;

    fprintf(stderr, "\rObjects to go : %"PRIu32"", nobj_to_write);
  }

  fprintf(stderr, "\nWrote %"PRIu32" objects out of %"PRIu32"\n",
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
  percent = strtol(argv[percent_offset], &endptr, 10);
  if ((ret = (*endptr != '%')))
  {
    fprintf(stderr, "PERCENT must be follwed by a '%%'");
    goto exit;
  }

  ret = fill(dev, md_dev, percent);

exit:
  return ret;
}
