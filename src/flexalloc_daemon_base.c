#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include "flexalloc_util.h"
#include "flexalloc_daemon_base.h"
// TODO: remove after debugging
#include <inttypes.h>

#define BUF_SIZE 1024 * 10
#define POLL_TIMEOUT_MS 1000
#define FLA_DAEMON_FD_FREE -1
#define FLA_DAEMON_POLL_INF -1

// TODO: handle CTRL-C, close down properly...
// TODO: handler fn - to make extensible
// TODO: test with str handler, start implementing REAL handler too (real msgs, real calls, real returns.)
// TODO: extra calls -- get pool obj nlb dimensions for read/write, probably cache on client side. (open/close pool...?)

int
fla_max_open_files()
{
  return sysconf(_SC_OPEN_MAX);
}

int
fla_daemon_create(struct fla_daemon *d, char *socket_path, fla_daemon_msg_handler_t on_msg,
                  int max_clients, int conn_queue_length)
{
  int err = 0;
  if (FLA_ERR(access(socket_path, F_OK) == 0,
              "fla_daemon_create(): file already exist at proposed UNIX socket path"))
    return -1;

  memset(d, 0, sizeof(struct fla_daemon));
  d->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (FLA_ERR_ERRNO(err = d->listen_fd < 0, "socket()"))
    goto exit;

  d->server.sun_family = AF_UNIX;
  if (FLA_ERR(strlen(socket_path) > sizeof(d->server.sun_path) -1, "socket path too long"))
  {
    err = -1;
    goto exit;
  }

  strncpy(d->server.sun_path, socket_path, sizeof(d->server.sun_path) - 1);

  // bind socket to address
  err = bind(d->listen_fd, (struct sockaddr *)&d->server, sizeof(d->server));
  if (FLA_ERR_ERRNO(err < 0, "bind()"))
    goto exit;

  // finally, listen, clients may now connect.
  if(FLA_ERR(listen(d->listen_fd, conn_queue_length) < 0, "listen()"))
  {
    err = -1;
    goto free_socket;
  }

  d->max_clients = max_clients;
  d->on_msg = on_msg;

  return 0;

free_socket:
  unlink(d->server.sun_path);
exit:
  return err;
}

int
fla_daemon_destroy(struct fla_daemon *d)
{
  int err = 0;
  // first close the socket (if needed)
  if (FLA_ERR_ERRNO(d->listen_fd && close(d->listen_fd) != 0, "close()"))
  {
    err = -1;
    goto exit;
  }
  else
  {
    d->listen_fd = 0;
  }

  // then remove the UNIX socket file (if it exists)
  if (d->server.sun_path[0] != '\0' && access(d->server.sun_path, F_OK))
  {
    if (FLA_ERR_ERRNO(unlink(d->server.sun_path), "unlink(): failed to remove socket file"))
    {
      err = -2;
      goto exit;
    }
    else
    {
      d->server.sun_path[0] = '\0';
    }
  }

exit:
  return err;
}

ssize_t
fla_sock_send_bytes(int sock_fd, char *buf, size_t n)
{
  ssize_t nleft = n, nwritten;

  while (nleft > 0)
  {
    if ((nwritten = send(sock_fd, buf, nleft, MSG_NOSIGNAL)) <= 0)
    {
      if (nwritten < 0 && errno == EINTR)
      {
        nwritten = 0;   /* and retry */
      }
      else
      {
        return -1;
      }
    }

    nleft -= nwritten;
    buf += nwritten;
  }

  return n;
}

void
fla_daemon_client_disconnect(struct pollfd *client, int *active_clients)
{
  FLA_DBG_PRINTF("disconnect client %d\n", client->fd);
  (*active_clients)--;
  close(client->fd);
  client->fd = FLA_DAEMON_FD_FREE;
}

int
fla_daemon_loop(struct fla_daemon *d,
                volatile sig_atomic_t *keep_running)
{
  int max_clients_ndx, i, nready, sockfd, connfd;
  int active_clients = 0;
  struct pollfd clients[d->max_clients];
  char buf[BUF_SIZE];
  socklen_t clilen;
  struct sockaddr_un cliaddr;
  struct msg_header hdr;
  size_t n;

  if (FLA_ERR(keep_running == NULL, "fla_daemon_loop(): keep_running argument cannot be null"))
    return -EINVAL;

  if (FLA_ERR(*keep_running == 0, "fla_daemon_loop(): keep_running arg is already unset"))
    return -EINVAL;

  clients[0].fd = d->listen_fd;
  clients[0].events = POLLIN;

  max_clients_ndx = 0;
  for (i = 1; i < d->max_clients; i++)
  {
    clients[i].fd = FLA_DAEMON_FD_FREE;
  }

  while (*keep_running || active_clients)
  {
    nready = poll(clients, max_clients_ndx + 1, POLL_TIMEOUT_MS);
    if (nready == 0 && *keep_running == 0)
    {
      fprintf(stderr, "waiting for %d clients to quit\n", active_clients);
      if (active_clients == 0)
        return 0;
    }
    else if (nready < 0)
    {
      continue;
    }

    // Check new connection
    if (clients[0].revents & POLLIN)
    {
      clilen = sizeof(cliaddr);
      connfd = accept(d->listen_fd, (struct sockaddr *)&cliaddr, &clilen);
      if (connfd < 0)
      {
        if (FLA_ERR_ERRNO(errno != EAGAIN
                          && errno != EWOULDBLOCK, "accept() - error accepting new client connection"))
        {
          return -1;
        }
      }
      FLA_DBG_PRINTF("new client, socket %d\n", connfd);

      // Save client socket into clients array
      for (i = 0; i < d->max_clients; i++)
      {
        if (clients[i].fd < 0)
        {
          clients[i].fd = connfd;
          break;
        }
      }

      // No enough space in clients array
      if (i == d->max_clients)
      {
        FLA_ERR_PRINTF("fla_daemon_loop() - too many clients! All %d slots taken\n", d->max_clients);
        close(connfd);
      }

      clients[i].events = POLLIN;

      if (i > max_clients_ndx)
      {
        max_clients_ndx = i;
      }
      active_clients++;

      // No more readable file descriptors
      if (--nready <= 0)
      {
        continue;
      }
    }

    // Check all clients to read data
    for (i = 1; i <= max_clients_ndx; i++)
    {
      if ((sockfd = clients[i].fd) < 0)
      {
        continue;
      }

      // If the client is readable or errors occur
      if (clients[i].revents & (POLLIN | POLLERR))
      {
        // read msg in two stages
        // 1: header (length and opcode)
        // 2: message
        n = read(sockfd, buf, sizeof(struct msg_header));

        if (n != sizeof(struct msg_header))
        {
          if (n < 0)
          {
            FLA_ERR_PRINTF("socket %d: read error\n", sockfd);
            fla_daemon_client_disconnect(&clients[i], &active_clients);
          }
          else if (n == 0)
          {
            FLA_DBG_PRINTF("socket %d: closed\n", sockfd);
            fla_daemon_client_disconnect(&clients[i], &active_clients);
          }
          else
          {
            FLA_ERR_PRINTF("socket %d: expected a msg_header (%zu bytes), got %zu bytes\n", sockfd,
                           sizeof(struct msg_header), n);
            fla_daemon_client_disconnect(&clients[i], &active_clients);
          }
          continue;
        }

        hdr = *((struct msg_header *)&buf);

        // 2: read message body
        n = read(sockfd, buf, hdr.oplen);

        if (n != hdr.oplen)
        {
          if (n < 0)
          {
            FLA_DBG_PRINTF("socket %d: read error\n", sockfd);
          }
          else if (n == 0)
          {
            // connection closed by client
            FLA_DBG_PRINTF("close socket %d\n", sockfd);
          }
          else
          {
            FLA_ERR_PRINTF("invalid message received, header indicated %"PRIu32" bytes, got %zu bytes\n",
                           hdr.oplen, n);
          }
          fla_daemon_client_disconnect(&clients[i], &active_clients);
        }
        else
        {
          if (FLA_ERR(d->on_msg(sockfd, &hdr, buf), "on_msg handler in fla_daemon_loop"))
          {
            fla_daemon_client_disconnect(&clients[i], &active_clients);
          }
        }

        // No more readable file descriptors
        if (--nready <= 0)
        {
          break;
        }
      }
    }
  }
  return 0;
}
