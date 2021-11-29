// Copyright (C) Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc.h"
#include "tests/flexalloc_tests_common.h"
#include "flexalloc_util.h"
#include "flexalloc_mm.h"
#include "libflexalloc.h"
#include <stdint.h>
#include <string.h>

struct ut_pool_entry
{
  char *name;
  struct fla_pool *handle;
};

struct ut_pool_entry pools[] =
{
  {.name = "pool1", .handle = NULL},
  {.name = "pool2", .handle = NULL},
  {.name = "pool3", .handle = NULL},
  {.name = "pool4", .handle = NULL},
};

int
__pool_handle_eq(struct fla_pool *h1, struct fla_pool *h2)
{
  return (h1->h2 == h2->h2 && h1->ndx == h2->ndx);
}

void
__pp_handle(FILE *stream, struct fla_pool *h)
{
  fprintf(stream, "handle{ndx: %"PRIu32", h2: %"PRIx64"}", h->ndx, h->h2);
}

size_t pools_len = sizeof(pools)/sizeof(struct ut_pool_entry);


int
lookup_and_validate_pool_handles(struct flexalloc *fs)
{
  /*
   * For each entry in the 'pools' array, find/lookup the pool by name.
   * Furthermore, compare the returned handle's data to the handle acquired
   * at the point of creation (see main function).
   */
  struct fla_pool * handle;
  int err = 0, ret;

  for(unsigned int i = 0; i < pools_len; i++)
  {
    ret = fla_pool_open(fs, pools[i].name, &handle) == 0;
    err |= FLA_ASSERTF(ret, "fla_pool_open(): pool (%s) not found", pools[i].name);

    if (err)
      goto exit;

    ret = __pool_handle_eq(handle, pools[i].handle);
    err |= FLA_ASSERT(ret,
                      "__pool_handle_eq() - acquired pool handle from lookup differs from expected");
    if (err)
    {
      FLA_ERR_PRINTF("pool '%s' (index: %u):\n", pools[i].name, i);
      FLA_ERR_PRINTF("   * handle(actual): {ndx: %"PRIu32", h2: %"PRIx64"}\n", handle->ndx,
                     handle->h2);
      FLA_ERR_PRINTF("   * handle(expected): {ndx: %"PRIu32", h2: %"PRIx64"}\n",
                     pools[i].handle->ndx,
                     pools[i].handle->h2);
      goto exit;
    }

    fla_pool_close(fs, handle);
  }

exit:
  return err;
}


int
main(int argc, char **argv)
{
  struct fla_ut_dev dev;
  struct flexalloc *fs = NULL;
  struct fla_pool * handle;
  int err = 0, ret;

  err = fla_ut_dev_init(40000, &dev);
  if (FLA_ERR(err, "fla_ut_dev_init()"))
  {
    goto exit;
  }

  if (dev._is_zns)
  {
    err = fla_ut_fs_create(dev.nsect_zn, 4, &dev, &fs);
  }
  else
  {
    err = fla_ut_fs_create(4000, 4, &dev, &fs);
  }

  if (FLA_ERR(err, "fla_ut_fs_create()"))
  {
    goto teardown_ut_dev;
  }

  // create pools
  for (unsigned int i = 0; i < pools_len; i++)
  {
    if (dev._is_zns)
    {
      ret = fla_pool_create(fs, pools[i].name, strlen(pools[i].name), dev.nsect_zn,
                            &pools[i].handle) == 0 ;
    }
    else
    {
      ret = fla_pool_create(fs, pools[i].name, strlen(pools[i].name), 2, &pools[i].handle) == 0;
    }

    err |= FLA_ASSERTF(ret,
                       "fla_pool_create(fs, name: %s, len: %u, obj_nlb: %u, handle) - initial acquire failed",
                       pools[i].name, strlen(pools[i].name), i);

    if (err)
      goto teardown_ut_fs;
  }

  // lookup pools and validate the returned handles
  err = lookup_and_validate_pool_handles(fs);
  if (FLA_ERR(err, "lookup_and_validate_pool_handles()"))
    goto teardown_ut_fs;

  // close flexalloc system - should flush changes to disk
  err = fla_close(fs);
  if (FLA_ERR(err, "fla_close()"))
    goto teardown_ut_fs;

  if (dev._md_dev_uri)
  {
    err = fla_md_open(dev._dev_uri, dev._md_dev_uri, &fs);
  }
  else
  {
    err = fla_open(dev._dev_uri, &fs);
  }

  if (FLA_ERR(err, "fla_open() - failed to re-open device"))
    goto teardown_ut_fs;

  // lookup pools and validate handles (again) - if still OK, changes persist across flexalloc open/close
  err = lookup_and_validate_pool_handles(fs);
  if (FLA_ERR(err, "lookup_and_validate_pool_handles()"))
    goto teardown_ut_fs;

  // destroy each pool
  for (unsigned int i = 0; i < pools_len; i++)
  {
    err = fla_pool_destroy(fs, pools[i].handle);
    if (FLA_ERR(err, "fla_pool_destroy()"))
      goto teardown_ut_fs;
  }

  // ensure pools cannot be found
  for (unsigned int i = 0; i < pools_len; i++)
  {
    err = fla_pool_open(fs, pools[i].name, &handle) == 0;
    if (FLA_ERR(err, "fla_pool_open() - found destroyed pool"))
    {
      __pp_handle(stderr, handle);
      fprintf(stderr, "\n");
      err |= 1;
    }
  }

  // close flexalloc system - changes should be persisted
  err = fla_close(fs);
  if (FLA_ERR(err, "fla_close()"))
    goto teardown_ut_fs;

  // We need to reopen the device to verify pools have been destroyed
  if (dev._is_zns)
  {
    err = fla_md_open(dev._dev_uri, dev._md_dev_uri, &fs);
  }
  else
  {
    err = fla_open(dev._dev_uri, &fs);
  }

  if (FLA_ERR(err, "fla_open() - failed to re-open device"))
    goto teardown_ut_fs;
  for (unsigned int i = 0; i < pools_len; i++)
  {
    err = fla_pool_open(fs, pools[i].name, &handle) == 0;
    if (FLA_ERR( err, "fla_pool_open() - found pool, should not have"))
    {
      __pp_handle(stderr, handle);
      fprintf(stderr, "\n");
      err |= 1;
    }
  }
  if (err)
    goto teardown_ut_fs;

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
