// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_daemon_base.h"
#include "flexalloc_util.h"
#include "libflexalloc.h"

#define SOCKET_PATH "/tmp/flexalloc.socket"


int
main(int argc, char **argv)
{
  int err = 0;
  struct fla_daemon_client client;

  if (FLA_ERR((err = fla_socket_open(SOCKET_PATH, &client)), "fla_socket_open()"))
    return -1;

  getchar();
  err = fla_close(client.flexalloc);
  if (FLA_ERR(err, "fla_close()"))
    return -1;
}
