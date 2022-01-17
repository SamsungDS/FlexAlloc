// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_daemon_base.h"
#include "flexalloc_util.h"
#include "libflexalloc.h"
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#define SOCKET_PATH "/tmp/flexalloc.socket"


int
main(int argc, char **argv)
{
  int err = 0;
  struct fla_daemon_client client;
  struct fla_pool *pool;

  memset(&client, 0, sizeof(struct fla_daemon_client));

  if (FLA_ERR((err = fla_daemon_open(SOCKET_PATH, &client)), "fla_daemon_open()"))
    return -1;

  // TODO: create variant program / option to open existing pool, check if it works
  err = fla_pool_create(client.flexalloc, "hello", 6, 10, &pool);
  if (FLA_ERR(err, "fla_pool_create()"))
  {
    return -1;
  }
  fprintf(stderr, "pool{h2: %"PRIu64", ndx: %"PRIu32"}\n", pool->h2, pool->ndx);

  err = fla_pool_destroy(client.flexalloc, pool);
  if (FLA_ERR(err, "fla_pool_destroy()"))
    return -1;

  err = fla_pool_create(client.flexalloc, "hello", 5, 10, &pool);
  if (FLA_ERR(err, "fla_pool_create() 2"))
    return -1;
  fprintf(stderr, "pool{h2: %"PRIu64", ndx: %"PRIu32"}\n", pool->h2, pool->ndx);

  fla_pool_close(client.flexalloc, pool);

  // TODO: TEST OPERATION
  //err = fla_pool_open(client.flexalloc, pool);

  getchar();
  err = fla_close(client.flexalloc);
  if (FLA_ERR(err, "fla_close()"))
    err = 1;

  return err;
}
