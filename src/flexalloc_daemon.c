// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_daemon_base.h"
#include "src/flexalloc_cli_common.h"
#include "src/flexalloc_util.h"
#include "libflexalloc.h"
#include <stdlib.h>
#include <inttypes.h>

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
  case FLA_MSG_CMD_SYNC_NO_RSPS:
    if (FLA_ERR(fla_daemon_sync_rsp(d, client_fd, recv, NULL), "fla_daemon_sync_rsp()"))
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

static struct cli_option options[] =
{
  {
    .base = {"socket", required_argument, NULL, 's'},
    .description = "path of where to create the UNIX socket",
    .arg_ex = "PATH"
  },
  {
    .base = {"device", required_argument, NULL, 'd'},
    .description = "path of device containing the FlexAlloc system",
    .arg_ex = "DEVICE"
  },
  {
    .base = {NULL, 0, NULL, 0}
  }
};

void
fla_daemon_usage()
{
  fprintf(stdout, "Usage: flexalloc_daemon [options]\n\n");
  fprintf(stdout, "Provide mediated access to a FlexAlloc system via a daemon\n\n");
  print_options(options);
}

int
main(int argc, char **argv)
{
  int err = 0;
  int c;
  int opt_idx = 0;
  char *socket_path = NULL;
  char *device = NULL;
  struct fla_daemon daemon;
  int const n_opts = sizeof(options)/sizeof(struct cli_option);
  struct option long_options[n_opts];
  struct fla_open_opts fla_oopts = {0};

  for (int i=0; i<n_opts; i++)
  {
    memcpy(long_options+i, &options[i].base, sizeof(struct option));
  }

  while ((c = getopt_long(argc, argv, "vs:d:", long_options, &opt_idx)) != -1)
  {
    switch (c)
    {
    case 's':
      socket_path = optarg;
      break;
    case 'd':
      device = optarg;
      break;
    default:
      break;
    }
  }

  if (!socket_path || !device)
  {
    fla_daemon_usage();
    err = 2;
    if (!socket_path)
      fprintf(stderr,
              "missing socket argument - must specify where to create the UNIX socket mediating access to FlexAlloc system\n");
    if (!device)
      fprintf(stderr, "missing device argument - must specify which device holds the FlexAlloc system\n");
    goto exit;
  }

  signal(SIGINT, sigint_handler);

  err = fla_daemon_create(&daemon, socket_path, msg_handler, MAX_CLIENTS, MAX_CONN_QUEUE);
  if (FLA_ERR(err, "failed to initialize"))
    return EXIT_FAILURE;

  daemon.identity.type = FLA_SYS_FLEXALLOC_TYPE;
  daemon.identity.version = FLA_SYS_FLEXALLOC_V1;

  fla_oopts.dev_uri = device;
  err = fla_open(&fla_oopts, &daemon.flexalloc);
  if (FLA_ERR(err, "fla_open()"))
    goto exit;

  fprintf(stderr, "daemon ready for connections...\n");
  err = fla_daemon_loop(&daemon, &keep_running);
  if (FLA_ERR(err, "failure operate"))
    goto close_dev;

  fprintf(stderr, "shutting down\n");
  err = fla_daemon_destroy(&daemon);
  if (FLA_ERR(err, "failure during cleanup"))
    goto close_dev;

  return 0;
close_dev:
  fla_close(daemon.flexalloc);
exit:
  return err;
}
