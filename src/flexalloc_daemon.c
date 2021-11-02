// Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
#include "flexalloc_daemon_base.h"
#include "src/flexalloc_util.h"
#include <stdlib.h>

#define SOCKET_PATH "/tmp/flexalloc.socket"
#define MAX_CLIENTS 110
#define MAX_CONN_QUEUE 100

volatile sig_atomic_t keep_running = 1;

// TODO: install other signal handler for next CTRL-C
static void
sigint_handler(int _)
{
  (void)_;
  keep_running = 0;
  fprintf(stderr, "SIGINT caught, gracefully shutting down daemon...\n");
}

int
echo_handler(int client_fd, struct msg_header *hdr, char *msg_buf)
{
  if (FLA_ERR(fla_daemon_send_bytes(client_fd, msg_buf, hdr->oplen) != hdr->oplen,
              "fla_daemon_send_bytes() - failed to send message"))
    return -1;
  return 0;
}

int
main(int argc, char **argv)
{
  struct fla_daemon daemon;
  int err = 0;

  signal(SIGINT, sigint_handler);

  err = fla_daemon_create(&daemon, SOCKET_PATH, echo_handler, MAX_CLIENTS, MAX_CONN_QUEUE);
  if (FLA_ERR(err, "failed to initialize"))
    return EXIT_FAILURE;

  err = fla_daemon_loop(&daemon, &keep_running);
  if (FLA_ERR(err, "failure operate"))
    return EXIT_FAILURE;

  fprintf(stderr, "shutting down\n");
  err = fla_daemon_destroy(&daemon);
  if (FLA_ERR(err, "failure during cleanup"))
    return EXIT_FAILURE;
}
