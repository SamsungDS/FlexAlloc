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
#include <inttypes.h>

#define BUF_SIZE 1024 * 10
#define POLL_TIMEOUT_MS 1000
#define FLA_DAEMON_FD_FREE -1
#define FLA_DAEMON_POLL_INF -1

int
fla_max_open_files()
{
  return sysconf(_SC_OPEN_MAX);
}

int
fla_sock_sockaddr_init(struct sockaddr_un *socket, const char * socket_path)
{
  socket->sun_family = AF_UNIX;
  if (FLA_ERR(strlen(socket_path) > sizeof(socket->sun_path) -1, "socket path too long"))
    return -1;

  strncpy(socket->sun_path, socket_path, sizeof(socket->sun_path) - 1);
  return 0;
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

  if (FLA_ERR(fla_sock_sockaddr_init(&d->server, socket_path), "fla_sock_sockaddr_init()"))
  {
    err = -1;
    goto exit;
  }

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

int
fla_sock_send_msg(int sock_fd, char *msg)
{
  size_t msg_size = sizeof(struct msg_header) + FLA_MSG_HDR(msg)->len;
  size_t sent = fla_sock_send_bytes(sock_fd, msg, msg_size);
  if (FLA_ERR(sent != msg_size, "fla_sock_send_bytes() - sent bytes differs from msg length"))
    return -1;
  return 0;
}

int
fla_sock_recv_msg(int sock_fd, char *msg)
{
  unsigned int retries = 3;
  ssize_t n, nleft;

retry_hdr:
  n = recv(sock_fd, msg, sizeof(struct msg_header), MSG_PEEK | MSG_WAITALL);
  if (n != sizeof(struct msg_header))
  {
    if (n > 0 || (n == -1 && errno == EINTR))
    {
      if (retries == 0)
        return -1;
      retries--;
      goto retry_hdr;
    }

    // TODO: errno reporting
    if (FLA_ERR_ERRNO(n == -1, "recv() - failed to receive msg header"))
      return -errno;
  }

  // ensure message data length is not exceeding protocol limits
  if ((FLA_MSG_HDR(msg)->len) > FLA_MSG_DATA_MAX)
  {
    struct msg_header *hdr = FLA_MSG_HDR(msg);
    FLA_ERR_PRINTF("invalid msg from socket %d, hdr{tag: %"PRIu32", len: %"PRIu32"}, max len is: %d\n",
                   sock_fd, hdr->tag, hdr->len, FLA_MSG_DATA_MAX);
    return -2; // TODO: document, this warrants dropping the client.
  }

  nleft = sizeof(struct msg_header) + FLA_MSG_HDR(msg)->len;
retry_data:
  while (nleft > 0)
  {
    if ((n = recv(sock_fd, msg, nleft, MSG_WAITALL)) <= 0)
    {
      if (errno == EINTR && retries != 0)
        goto retry_data;

      return -errno; // TODO: errno reporting
    }
    else
    {
      nleft -= n;
      msg += n;
    }
  }
  return 0;
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
  char buf[FLA_MSG_BUFSIZ];
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
        // 1: header (length and tag)
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
        n = read(sockfd, buf, hdr.len);

        if (n != hdr.len)
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
                           hdr.len, n);
          }
          fla_daemon_client_disconnect(&clients[i], &active_clients);
        }
        else
        {
          if (FLA_ERR(d->on_msg(d, sockfd, &hdr, buf), "on_msg handler in fla_daemon_loop"))
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

// int
// fla_get_proto(struct fla_daemon_client *client)
// {
//   int err;
//   struct msg_header *hdr = FLA_MSG_HDR(client->snd_buf);
//   //char *data = FLA_MSG_DATA(client->snd_buf);
//
//   hdr->tag = 1;
//   hdr->len = 0;
//
//   if (FLA_ERR((err = fla_sock_send_msg(client->sock_fd, client->snd_buf)), "fla_sock_send_msg()"))
//     return -1;
//
//
// }

int
fla_daemon_identify_req(struct fla_daemon_client *client, int sock_fd)
{
  struct msg_header *hdr = FLA_MSG_HDR(client->snd_buf);
  hdr->len = 0;
  hdr->tag = FLA_MSG_TAG_IDENTIFY_RQ;
  if (FLA_ERR(fla_sock_send_msg(sock_fd, client->snd_buf), "fla_daemon_msg_identify_req()"))
    return -1;
  return 0;
}

int
fla_daemon_identify_rsp(int client_fd, char *rcv_buf, char *snd_buf)
{
  struct msg_header *snd_hdr = FLA_MSG_HDR(snd_buf);
  char *data = FLA_MSG_DATA(snd_buf);
  snd_hdr->tag = FLA_MSG_TAG_IDENTIFY_RSP;
  snd_hdr->len = 1332323; // TODO change
  fla_sock_send_msg(client_fd, snd_buf);
}

int
fla_socket_open(const char *socket_path, struct fla_daemon_client *client)
{
  int err;
  struct sockaddr_un server;

  client->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (FLA_ERR_ERRNO(err = client->sock_fd < 0, "socket()"))
    goto exit;

  if (FLA_ERR(err = fla_sock_sockaddr_init(&server, socket_path), "fla_sock_sockaddr_init()"))
    goto exit;

  if (FLA_ERR(err = connect(client->sock_fd, (struct sockaddr *)&server, sizeof(server)),
              "connect()"))
    goto exit;

  FLA_DBG_PRINT("connected to server\n");

  // MAYBE reset snd/rcv bufs.
  // get version msg
//   struct sockaddr_un server;
//
//   err = fla_fs_alloc(fs);
//   if (err)
//     return err;
//
//
//   err = fla_xne_dev_open(dev_uri, NULL, &dev);
//   if (FLA_ERR(err, "fla_xne_dev_open()"))
//   {
//     err = FLA_ERR_ERROR;
//     return err;
//   }
exit:
  return err;
}
