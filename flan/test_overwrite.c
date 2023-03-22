#include <errno.h>
#include <libflexalloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flan.h"
#include "flexalloc_util.h"

#define POOL_NAME "TEST"
#define USAGE "./test OBJ_SZ"

int open_and_write_object(int obj_size, char *dev_uri, char *message,
                          size_t offset);
int read_object(uint64_t obj_handle, size_t buf_size,
                struct flan_handle *flanh);

int main(int argc, char **argv) {
  char *dev_uri = getenv("FLAN_TEST_DEV");
  int err = 0;
  errno = 0;

  if (!dev_uri) {
    printf("Set env var FLAN_TEST_DEV in order to run test\n");
    return 0;
  }

  if (argc != 2) {
    printf("Object size not supplied, Usage:\n%s\n", USAGE);
    return 0;
  }

  char *endofarg;
  unsigned int object_size = strtoul(argv[1], &endofarg, 0);
  if (endofarg == argv[1]) {
    printf("Object size not found in argument\n");
    goto exit;
  }

  char *to_write = "My favourite food is salad!";
  err = open_and_write_object(object_size, dev_uri, to_write, 0);
  if (err) {
    printf("Test run 1 failed");
    goto exit;
  }

  printf("Closing, re-opening\n");

  to_write = "pizza!";
  err = open_and_write_object(object_size, dev_uri, to_write, 21);
  if (err) {
    printf("Test run 2 failed");
    goto exit;
  }

  printf("Closing, re-opening\n");

  to_write = "chocolate!";
  err = open_and_write_object(object_size, dev_uri, to_write, 21);
  if (err) {
    printf("Test run 3 failed");
    goto exit;
  }

exit:
  return err;
}

int open_and_write_object(int obj_size, char *dev_uri, char *message, size_t offset) {
  struct flan_handle *flanh = NULL;
  int err = 0;

  struct fla_pool_create_arg pool_arg = {.flags = 0,
                                         .name = POOL_NAME,
                                         .name_len = strlen(POOL_NAME),
                                         .obj_nlb =
                                             0, // will get set by flan_init
                                         .strp_nobjs = 0,
                                         .strp_nbytes = 0};

  err = flan_init(dev_uri, NULL, &pool_arg, obj_size, &flanh);
  if (err) {
    printf("Failed to init flan\n");
    goto exit;
  }

  size_t buf_size = obj_size;
  char *obj_name = "im-hungry";

  uint64_t obj_handle;
  err = flan_object_open(obj_name, flanh, &obj_handle,
                         FLAN_OPEN_FLAG_CREATE | FLAN_OPEN_FLAG_WRITE);
  if (err) {
    printf("Failed to open object '%s'\n", obj_name);
    goto exit;
  }

  char *buf = flan_buf_alloc(buf_size, flanh);
  if (!buf) {
    printf("Failed to allocate buffer\n");
    goto exit;
  }

  printf("Before object write, ");
  err = read_object(obj_handle, buf_size, flanh);
  if (err) {
    printf("Failed to read object\n");
    goto close_object;
  }

  memset(buf, '!', buf_size);
  strcpy(buf, message);
  err = flan_object_write(obj_handle, buf, offset, strlen(message) + 1, flanh);
  if (err) {
    printf("Failed to write object '%s'\n", obj_name);
    goto close_object;
  }

  printf("After object write, ");
  err = read_object(obj_handle, buf_size, flanh);
  if (err) {
    printf("Failed to read object\n");
    goto close_object;
  }

close_object:
  err = flan_object_close(obj_handle, flanh);
  if (err) {
    printf("Failed to close object '%s'\n", obj_name);
    goto exit;
  }

exit:
  if (flanh) {
    flan_close(flanh);
  }

  return err;
}

int read_object(uint64_t obj_handle, size_t buf_size,
                struct flan_handle *flanh) {
  int err = 0;

  char *buf = flan_buf_alloc(buf_size, flanh);
  if (!buf) {
    printf("Failed to allocate buffer\n");
    err = 1;
    goto exit;
  }

  size_t num_bytes_read = flan_object_read(obj_handle, buf, 0, buf_size, flanh);

  printf("read data (%ld bytes): %s\n", num_bytes_read, buf);
  // printf("Bytes: ");
  // for (int i = 0; i < num_bytes_read; i++)
  //   printf("%02X ", buf[i]);
  // printf("\n");

exit:
  return err;
}
