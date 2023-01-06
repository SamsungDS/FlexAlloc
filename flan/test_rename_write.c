// Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
#include <libflexalloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "flexalloc_util.h"

#include "flan.h"

#define POOL_NAME "TEST"
#define USAGE "./test OBJ_SZ"
#define FLAN_TEST_MAX_OBJECTS_TO_CREATE 5

int main(int argc, char **argv)
{
  struct flan_handle *flanh;
  uint64_t oh;
  int ret = 0;
  char *dev_uri = getenv("FLAN_TEST_DEV");
  char *obj_name = NULL, *obj_new_name = NULL;
  char *md_dev_uri = getenv("FLAN_TEST_MD_DEV");
  unsigned int obj_sz;
  unsigned int mid;
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


  mid = obj_sz / 2;
  // Open flexalloc device, create pool, create named object and close
  printf("Opening flan, and creating \"test\" objects\n");
  if (md_dev_uri)
    ret = flan_init(dev_uri, md_dev_uri, &pool_arg, obj_sz, &flanh);
  else
    ret = flan_init(dev_uri, NULL, &pool_arg, obj_sz, &flanh);

  if (ret)
    return FLA_ERR(ret, "flan_init()");

  obj_name = flan_buf_alloc(obj_sz, flanh);
  if (!obj_name)
    return FLA_ERR(-EIO, "flan_buf_alloc()");

  for (int count = 0 ; count < FLAN_TEST_MAX_OBJECTS_TO_CREATE; ++count)
  {
    snprintf(obj_name, FLAN_OBJ_NAME_LEN_MAX, "%s.%d", POOL_NAME, count);

    ret = flan_object_open(obj_name, flanh, &oh, FLAN_OPEN_FLAG_CREATE | FLAN_OPEN_FLAG_WRITE);
    if (FLA_ERR(ret, "flan_object_open()"))
      goto out;

    ret = flan_object_write(oh, obj_name, 0, mid - 1, flanh);
    if (FLA_ERR(ret, "flan_object_write()"))
      goto out;

    flan_object_read(oh, obj_name, 0, mid - 1, flanh);

    ret = flan_object_close(oh, flanh);
    if (FLA_ERR(ret, "flan_object_close()"))
      goto out;
  }

  obj_new_name = flan_buf_alloc(FLAN_OBJ_NAME_LEN_MAX, flanh);
  if (!obj_new_name)
    return FLA_ERR(-EIO, "flan_buf_alloc()");

  /* rename all with _ instead of .*/
  for (int count = 0 ; count < FLAN_TEST_MAX_OBJECTS_TO_CREATE; ++count)
  {
    snprintf(obj_name, FLAN_OBJ_NAME_LEN_MAX, "%s.%d", POOL_NAME, count);
    snprintf(obj_new_name, FLAN_OBJ_NAME_LEN_MAX, "%s_%d", POOL_NAME, count);

    ret = flan_object_open(obj_name, flanh, &oh, FLAN_OPEN_FLAG_CREATE | FLAN_OPEN_FLAG_WRITE);
    if (FLA_ERR(ret, "flan_object_open()"))
      goto out;

    ret = flan_object_rename(obj_name, obj_new_name, flanh);
    if (FLA_ERR(ret, "flan_object_rename()"))
      goto out;

    ret = flan_object_close(oh, flanh);
    if (FLA_ERR(ret, "flan_object_close()"))
      goto out;
  }

  /* rewrite the changed names */
  for (int count = 0 ; count < FLAN_TEST_MAX_OBJECTS_TO_CREATE; ++count)
  {
    snprintf(obj_name, FLAN_OBJ_NAME_LEN_MAX, "%s_%d", POOL_NAME, count);

    ret = flan_object_open(obj_name, flanh, &oh, FLAN_OPEN_FLAG_READ);
    if (FLA_ERR(ret, "flan_object_open()"))
      goto out;

    flan_object_read(oh, obj_name, 0, mid - 1, flanh);

    ret = flan_object_close(oh, flanh);
    if (FLA_ERR(ret, "flan_object_close()"))
      goto out;
  }


  flan_close(flanh);
  free(obj_name);

out:
  return ret;

}
