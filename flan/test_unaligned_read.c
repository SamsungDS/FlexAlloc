#include <libflexalloc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "flan.h"
#include "../tests/flexalloc_tests_common.h"
#include "flexalloc_util.h"

#define POOL_NAME "TEST"
#define USAGE "./test OBJ_SZ"
#define FLAN_TEST_MAX_OBJECTS_TO_CREATE 5

int main(int argc, char **argv)
{
  struct flan_handle *flanh;
  uint64_t oh;
  int ret = 0;
  char *dev_uri = getenv("FLAN_TEST_DEV");
  char *md_dev_uri = getenv("FLAN_TEST_MD_DEV");
  unsigned int obj_sz;
  char *endofarg;
  char *obj_name = NULL, *w_buf, *r_buf;

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

  /* We allocate a write and read buffer that is bigger than the flan
   * read buffer size*/
  size_t buf_sz = FLAN_APPEND_SIZE * 5;
  r_buf = flan_buf_alloc(buf_sz, flanh);
  if (!r_buf)
    goto out_close;
  w_buf = flan_buf_alloc(buf_sz, flanh);
  if (!r_buf)
    goto out_close;

  fla_t_fill_buf_random(w_buf, buf_sz);
  char * obj_n = "test01";
  ret = flan_object_open(obj_n, flanh, &oh, FLAN_OPEN_FLAG_CREATE | FLAN_OPEN_FLAG_WRITE);
  if (ret)
    goto out_close;

  uint64_t offset = obj_sz - (2*FLAN_APPEND_SIZE);
  ret = flan_object_write(oh, w_buf, offset, buf_sz, flanh);
  if (ret)
    goto out_close;

  ret = flan_object_close(oh, flanh);
  if (ret)
    goto out_close;

  ret = flan_object_open(obj_n, flanh, &oh, FLAN_OPEN_FLAG_READ);
  if (ret)
    goto out_close;


  size_t l_offset = FLAN_APPEND_SIZE / 2;
  size_t l_step = 1000;
  size_t l_len = buf_sz - l_offset;

  while (l_offset > l_step)
  {
    ret = flan_object_read(oh, r_buf, offset + l_offset, l_len, flanh);
    if(ret != l_len)
      goto out_close;

    ret = memcmp(r_buf, w_buf + l_offset, l_len);
    if (FLA_ERR(ret, "The read was different from written in unaliged read"))
      goto out_close;

    l_len += l_step;
    l_offset -= l_step;
  }

  ret = flan_object_close(oh, flanh);
  if (ret)
    goto out_close;

out_close:
  flan_close(flanh);

out:
  if (obj_name)
    free(obj_name);

  return ret;
}
