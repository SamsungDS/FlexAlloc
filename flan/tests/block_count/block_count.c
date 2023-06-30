#include "flan.h"
#include <stdio.h>

#define POOL_NAME "DEMO_POOL"
// Fails when opening object 29
#define OBJ_COUNT_TO_WRITE 30

int main(int argc, char *argv[]) {
  uint64_t obj_size = 4096;
  char *device_uri = NULL;
  char *obj_name = NULL;

  if (argc == 3) {
    device_uri = argv[1];
    obj_size = atoi(argv[2]);
  } else {
    printf("Usage: %s [device_uri] [obj_size]\n", argv[0]);
    return 1;
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

  printf("Opening flan\n");
  struct flan_handle *flanh = NULL;
  if (flan_init(device_uri, NULL, &pool_arg, obj_size, &flanh)) {
    perror("Failed to initialise flan\n");
    goto end;
  }
  printf("Initialised flan\n");

  for (int i = 0; i < OBJ_COUNT_TO_WRITE; i++) {
    char *obj_name = flan_buf_alloc(FLAN_OBJ_NAME_LEN_MAX, flanh);
    if (!obj_name) {
      perror("Failed to allocate name buffer\n");
      goto end;
    }

    // we just arbitrarily choose an object name format: "DEMO_OBJ_.<some id>"
    snprintf(obj_name, FLAN_OBJ_NAME_LEN_MAX, "DEMO_OBJ_%d", i);

    char *buf = flan_buf_alloc(obj_size, flanh);
    if (!buf) {
      perror("Failed to allocate buffer\n");
      goto end;
    }

    printf("Opening object %s\n", obj_name);
    uint64_t obj_handle;
    if (flan_object_open(obj_name, flanh, &obj_handle, FLAN_OPEN_FLAG_CREATE | FLAN_OPEN_FLAG_WRITE)) {
      perror("Failed to open object");
      goto end;
    }

    /*printf("Writing data\n");
    for (int j = 0; j < obj_size; j++) {
      buf[j] = '0' + (j % 10);
    }
    // Print the buffer
    for (int j = 0; j < obj_size; j++) {
      printf("%c", buf[j]);
    }
    printf("\n");
    */

    if (flan_object_write(obj_handle, buf, 0, obj_size, flanh)) {
      perror("Failed to write object");
      goto end;
    }

    printf("Closing object %s\n", obj_name);
    if (flan_object_close(obj_handle, flanh)) {
      perror("Failed to close object");
      goto end;
    }
  }

end:
  if (flanh) {
    printf("Closing flan\n");
    flan_close(flanh);
  }

  if (obj_name) {
    free(obj_name);
  }

  return 0;
}
