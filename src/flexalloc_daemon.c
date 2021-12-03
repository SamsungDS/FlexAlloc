// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_daemon_base.h"
#include "src/flexalloc_util.h"
#include "libflexalloc.h"
#include <stdlib.h>
#include <inttypes.h>

#define SOCKET_PATH "/tmp/flexalloc.socket"
#define MAX_CLIENTS 110
#define MAX_CONN_QUEUE 100

volatile sig_atomic_t keep_running = 1;

// TODO: refactor fla_open() to do what fla_open_common does (not allocate)
int
fla_open_common(char const *dev_uri, struct flexalloc *fs);

// TODO: install other signal handler for next CTRL-C
static void
sigint_handler(int _)
{
  (void)_;
  keep_running = 0;
  fprintf(stderr, "SIGINT caught, gracefully shutting down daemon...\n");
}

int
msg_handler(struct fla_daemon *d, int client_fd, struct fla_msg const * const recv,
            struct fla_msg const * const send)
{
  FLA_DBG_PRINTF("received msg hdr: {cmd: %"PRIu32", len: %"PRIu32"}\n", recv->hdr->cmd,
                 recv->hdr->len);
  switch (recv->hdr->cmd)
  {
  case FLA_MSG_CMD_OBJECT_OPEN:
    if (FLA_ERR(fla_daemon_object_open_rsp(d, client_fd, recv, send), "fla_daemon_object_open()"))
      return -1;
    break;
  case FLA_MSG_CMD_OBJECT_CREATE:
    if (FLA_ERR(fla_daemon_object_create_rsp(d, client_fd, recv, send), "fla_daemon_object_create()"))
      return -1;
    break;
  case FLA_MSG_CMD_OBJECT_DESTROY:
    if (FLA_ERR(fla_daemon_object_destroy_rsp(d, client_fd, recv, send), "fla_daemon_object_destroy()"))
      return -1;
    break;
  case FLA_MSG_CMD_POOL_OPEN:
    if (FLA_ERR(fla_daemon_pool_open_rsp(d, client_fd, recv, send), "fla_daemon_pool_open()"))
      return -1;
    break;
  case FLA_MSG_CMD_POOL_CREATE:
    if (FLA_ERR(fla_daemon_pool_create_rsp(d, client_fd, recv, send), "fla_daemon_pool_create()"))
      return -1;
    break;
  case FLA_MSG_CMD_POOL_DESTROY:
    if (FLA_ERR(fla_daemon_pool_destroy_rsp(d, client_fd, recv, send), "fla_daemon_pool_destroy()"))
      return -1;
    break;
  case FLA_MSG_CMD_SYNC:
    if (FLA_ERR(fla_daemon_sync_rsp(d, client_fd, recv, send), "fla_daemon_sync_rsp()"))
      return -1;
    break;
  case FLA_MSG_CMD_POOL_GET_ROOT_OBJECT:
    if (FLA_ERR(fla_daemon_pool_get_root_object_rsp(d, client_fd, recv, send),
                "fla_daemon_pool_get_root_object()"))
      return -1;
    break;
  case FLA_MSG_CMD_POOL_SET_ROOT_OBJECT:
    if (FLA_ERR(fla_daemon_pool_set_root_object_rsp(d, client_fd, recv, send),
                "fla_daemon_pool_set_root_object()"))
      return -1;
    break;
  case FLA_MSG_CMD_IDENTIFY:
    if (FLA_ERR(fla_daemon_identify_rsp(d, client_fd, recv, send), "fla_daemon_identify_rsp()"))
      return -1;
    break;
  case FLA_MSG_CMD_INIT_INFO:
    FLA_DBG_PRINT("FLA_MSG_CMD_INIT_INFO\n");
    if (FLA_ERR(fla_daemon_fs_init_rsp(d, client_fd, recv, send), "fla_daemon_init_info()"))
      return -1;
    break;
  default:
    FLA_ERR_PRINTF("socket %d: malformed message, msg cmd %"PRIu32"\n", client_fd, recv->hdr->cmd);
    return -1;
  }
  return 0;
}

int
main(int argc, char **argv)
{
  struct fla_daemon daemon;
  int err = 0;
  char *dev_uri;

  if (argc != 2)
  {
    fprintf(stderr, "USAGE: flexalloc_daemon <disk>\n");
    err = -1;
    goto exit;
  }
  dev_uri = argv[1];

  signal(SIGINT, sigint_handler);

  err = fla_daemon_create(&daemon, SOCKET_PATH, msg_handler, MAX_CLIENTS, MAX_CONN_QUEUE);
  if (FLA_ERR(err, "failed to initialize"))
    return EXIT_FAILURE;

  daemon.identity.type = FLA_SYS_FLEXALLOC_TYPE;
  daemon.identity.version = FLA_SYS_FLEXALLOC_V1;

  // TODO open device, assign vtable
  err = fla_open_common(dev_uri, daemon.flexalloc);
  if (FLA_ERR(err, "fla_open_common()"))
    goto exit;

  err = fla_daemon_loop(&daemon, &keep_running);
  if (FLA_ERR(err, "failure operate"))
    goto close_dev;

  fprintf(stderr, "shutting down\n");
  err = fla_daemon_destroy(&daemon);
  if (FLA_ERR(err, "failure during cleanup"))
    goto close_dev;

close_dev:
  fprintf(stderr, "TODO: will get free(): invalid size -- close tries to free flexalloc struct\n");
  fla_close(daemon.flexalloc);
exit:
  return err;
}
