// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>

#include <libflexalloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "flan.h"
#include "flexalloc_util.h"

#define POOL_NAME "TEST"
#define USAGE "./test OBJ_SZ"
#define FLAN_TEST_MAX_OBJECTS_TO_CREATE 5

int main(int argc, char **argv)
{
  struct flan_handle *flanh;
  uint64_t oh;
  int ret = 0;
  char *dev_uri = getenv("FLAN_TEST_DEV"), *obj_buf = NULL;
  char *md_dev_uri = getenv("FLAN_TEST_MD_DEV");
  char *obj_name = "OBJECT.1";
  unsigned int obj_sz;
  char *endofarg;

  if (!dev_uri)
  {
    printf("Set env var FLAN_TEST_DEV in order to run test\n");
    return 0;
  }

  if (argc != 2)
  {
    printf("Object size not supplied, Usasge:\n%s\n", USAGE);
    return 0;
  }

  errno = 0;
  obj_sz = strtoul(argv[1], &endofarg, 0);
  if (endofarg == argv[1])
  {
    printf("Number not found in argument\n");
    return 0;
  }

  if (errno)
  {
    perror("strotoul");
    return 0;
  }
  struct fla_pool_create_arg pool_arg =
  {
    .flags = 0,
    .name = POOL_NAME,
    .name_len = strlen(POOL_NAME),
    .obj_nlb = 0, // will get set by flan_init
    .strp_nobjs = 0,
    .strp_nbytes = 0
  };

  // Open flexalloc device, create pool, create named object and close
  printf("Opening flan, and creating \"test\" objects\n");
  if (md_dev_uri)
    ret = flan_init(dev_uri, md_dev_uri, &pool_arg, obj_sz, &flanh);
  else
    ret = flan_init(dev_uri, NULL, &pool_arg, obj_sz, &flanh);

  if (ret)
    goto out;

  unsigned int obj_buf_sz = obj_sz + (obj_sz * 0.2);
  obj_buf = flan_buf_alloc(obj_buf_sz, flanh);
  if (!obj_buf)
    goto out_close;

  ret = flan_object_open(obj_name, flanh, &oh, FLAN_OPEN_FLAG_CREATE | FLAN_OPEN_FLAG_WRITE);
  if (FLA_ERR(ret, "flan_object_open()"))
    goto out_close;

  ret = flan_object_write(oh, obj_buf, 0, obj_buf_sz, flanh);
  if (FLA_ERR(ret, "flan_object_write()"))
    goto out_close;

out_close:
  flan_close(flanh);

out:
  if (obj_buf)
    free(obj_buf);

  return ret;
}
