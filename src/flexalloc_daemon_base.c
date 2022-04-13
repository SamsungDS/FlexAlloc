#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <libxnvmec.h>
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
#include "src/flexalloc.h"
#include "src/flexalloc_mm.h"
#include "src/flexalloc_shared.h"
#include "src/flexalloc_xnvme_env.h"
#include <inttypes.h>

#define BUF_SIZE 1024 * 10
#define POLL_TIMEOUT_MS 1000
#define FLA_DAEMON_FD_FREE -1
#define FLA_DAEMON_POLL_INF -1


struct fla_daemon_client *fla_get_client(struct flexalloc const * const fs)
{
  return (struct fla_daemon_client *)fs->user_data;
}

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
  d->flexalloc = fla_fs_alloc();
  if (FLA_ERR(!d->flexalloc, "fla_fs_alloc()"))
    return -ENOMEM;

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

int
fla_sock_send_bytes(int sock_fd, char *buf, size_t n)
{
  ssize_t nleft = n, nwritten;
  errno = 0;

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
        return errno ? -errno : 1;
      }
    }

    nleft -= nwritten;
    buf += nwritten;
  }

  return 0;
}

int
fla_sock_send_msg(int sock_fd, struct fla_msg const * const msg)
{
  int err;
  size_t msg_size = sizeof(struct fla_msg_header) + msg->hdr->len;
  err = fla_sock_send_bytes(sock_fd, (char *)msg->hdr, msg_size);
  if (err)
  {
    if (err < 0)
    {
      FLA_ERR_PRINTF("fla_sock_send_msg() - error sending message, errno code: %d\n", -err);
    }
    else
    {
      FLA_ERR_PRINT("fla_sock_send_msg() - error sending message\n")
    }
  }

  return err;
}

int
fla_sock_recv_msg(int sock_fd, struct fla_msg const * const msg)
{
  int err = 0;
  unsigned int retries = 3;
  ssize_t n, nleft = -1;
  char *data;

  // use as sanity-check value in case of disconnects
  msg->hdr->cmd = FLA_MSG_CMD_NULL;

retry_hdr:
  n = recv(sock_fd, (char *)msg->hdr, sizeof(struct fla_msg_header), MSG_PEEK | MSG_WAITALL);
  if (n != sizeof(struct fla_msg_header))
  {
    if (n > 0 || (n == -1 && errno == EINTR))
    {
      if (retries == 0)
      {
        FLA_ERR_PRINT("recv() - retry limit exceeded, giving up!\n");
        err = 1;
        goto exit;
      }
      retries--;
      goto retry_hdr;
    }

    if (n == -1)
    {
      FLA_ERR_PRINTF("recv() - error receiving message header (errno: %d)\n", errno);
      return -errno;
    }
  }

  // ensure message data length is not exceeding protocol limits
  if (msg->hdr->len > FLA_MSG_DATA_MAX)
  {
    FLA_ERR_PRINTF("invalid msg from socket %d, hdr{cmd: %"PRIu32", len: %"PRIu32"}, max len is: %d\n",
                   sock_fd, msg->hdr->cmd, msg->hdr->len, FLA_MSG_DATA_MAX);
    err = 2;
    goto exit; // TODO: document, this warrants dropping the client
  }

  nleft = sizeof(struct fla_msg_header) + msg->hdr->len;
  data = (char *)msg->hdr;

retry_data:
  while (nleft > 0)
  {
    if ((n = recv(sock_fd, data, nleft, MSG_WAITALL)) <= 0)
    {
      if (errno == EINTR && retries != 0)
      {
        retries--;
        goto retry_data;
      }

      FLA_ERR_PRINTF("recv() - error receiving message payload (errno: %d)\n", errno);
      err = -errno;
      goto exit;
    }
    else
    {
      nleft -= n;
      data += n;
    }
  }

  // return success unless CMD value is still NULL
  err = msg->hdr->cmd == FLA_MSG_CMD_NULL;

exit:
  return err;
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
  char recv_buf[FLA_MSG_BUFSIZ];
  char send_buf[FLA_MSG_BUFSIZ];
  struct fla_msg const recv_msg =
  {
    .hdr = FLA_MSG_HDR(recv_buf),
    .data = FLA_MSG_DATA(recv_buf)
  };
  struct fla_msg const send_msg =
  {
    .hdr = FLA_MSG_HDR(send_buf),
    .data = FLA_MSG_DATA(send_buf)
  };

  socklen_t clilen;
  struct sockaddr_un cliaddr;
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
          return 1;
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
        // 1: header (length and cmd)
        // 2: message
        n = read(sockfd, recv_msg.hdr, sizeof(struct fla_msg_header));

        if (n != sizeof(struct fla_msg_header))
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
                           sizeof(struct fla_msg_header), n);
            fla_daemon_client_disconnect(&clients[i], &active_clients);
          }
          continue;
        }

        // 2: read message body
        n = read(sockfd, recv_msg.data, recv_msg.hdr->len);

        if (n != recv_msg.hdr->len)
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
                           recv_msg.hdr->len, n);
          }
          fla_daemon_client_disconnect(&clients[i], &active_clients);
        }
        else
        {
          send_msg.hdr->cmd = recv_msg.hdr->cmd;
          // correlate (potential) reply with request.
          send_msg.hdr->tag = recv_msg.hdr->tag;
          if (FLA_ERR(d->on_msg(d, sockfd,
                                (struct fla_msg const * const)&recv_msg,
                                (struct fla_msg const * const)&send_msg), "on_msg handler in fla_daemon_loop"))
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


int
fla_send_recv(struct fla_daemon_client *client)
{
  int err;
  err = fla_sock_send_msg(client->sock_fd, &client->send);
  if (FLA_ERR(err, "fla_sock_send_msg()"))
    goto exit;

  err = fla_sock_recv_msg(client->sock_fd, &client->recv);
  if (FLA_ERR(err, "fla_sock_recv_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_identify_rq(struct fla_daemon_client *client, int sock_fd,
                       struct fla_sys_identity *identity)
{
  int err;
  client->send.hdr->len = 0;
  client->send.hdr->cmd = FLA_MSG_CMD_IDENTIFY;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    return err;

  memcpy(identity, client->recv.data, sizeof(struct fla_sys_identity));

  return 0;
}

int
fla_daemon_identify_rsp(struct fla_daemon *daemon, int client_fd,
                        struct fla_msg const * const recv,
                        struct fla_msg const * const send)
{
  int err;

  memcpy(send->data, &daemon->identity, sizeof(struct fla_sys_identity));
  send->hdr->len = sizeof(struct fla_sys_identity);

  if (FLA_ERR((err = fla_sock_send_msg(client_fd, send)), "fla_sock_send_msg()"))
    return err;

  return 0;
}

int
fla_daemon_fs_init_rq(struct fla_daemon_client *client, int sock_fd)
{
  int err;
  struct xnvme_dev *dev = NULL, *md_dev = NULL;
  char *read_ptr = client->recv.data;
  size_t dev_uri_len, md_dev_uri_len;

  memset(client->send.data, 0, FLA_MSG_DATA_MAX);
  memset(client->recv.data, 0, FLA_MSG_DATA_MAX);

  client->send.hdr->len = 0;
  client->send.hdr->cmd = FLA_MSG_CMD_INIT_INFO;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    return err;

  memcpy(&client->flexalloc->geo, read_ptr, sizeof(struct fla_geo));
  client->flexalloc->dev.lb_nbytes = client->flexalloc->geo.lb_nbytes;
  read_ptr += sizeof(struct fla_geo);

  memcpy(&dev_uri_len, read_ptr, sizeof(size_t));
  read_ptr += sizeof(size_t);
  client->flexalloc->dev.dev_uri = strndup(read_ptr, dev_uri_len);
  if (!client->flexalloc->dev.dev_uri)
  {
    err = -ENOMEM;
    return err;
  }

  read_ptr += dev_uri_len;
  memcpy(&md_dev_uri_len, read_ptr, sizeof(size_t));
  if(md_dev_uri_len == 0)
    client->flexalloc->dev.md_dev_uri = NULL;
  else
  {
    read_ptr += sizeof(size_t);
    client->flexalloc->dev.md_dev_uri = strndup(read_ptr, md_dev_uri_len);
    if (!client->flexalloc->dev.md_dev_uri)
    {
      err = -ENOMEM;
      return err;
    }
  }

  client->flexalloc->pools.entries = calloc(sizeof(struct fla_pool_entry),
                                     client->flexalloc->geo.npools);
  if (!client->flexalloc->pools.entries)
  {
    err = -ENOMEM;
    goto free_dev_uri;
  }

  err = fla_xne_dev_open(client->flexalloc->dev.dev_uri, NULL, &dev);
  if (FLA_ERR(err, "fla_xne_dev_open() - failed to open device"))
    goto free_pool_entry_array;
  client->flexalloc->dev.dev = dev;

  if(md_dev_uri_len > 0)
  {
    err = fla_xne_dev_open(client->flexalloc->dev.md_dev_uri, NULL, &md_dev);
    if (FLA_ERR(err, "fla_xne_dev_open() - failed to open device"))
      goto free_pool_entry_array;
    client->flexalloc->dev.md_dev = md_dev;
  }

  return 0;

free_pool_entry_array:
  free(client->flexalloc->pools.entries);
  client->flexalloc->pools.entries = NULL;
free_dev_uri:
  free(client->flexalloc->dev.dev_uri);
  client->flexalloc->dev.dev_uri = NULL;

  return err;
}

int
fla_daemon_fs_init_rsp(struct fla_daemon *daemon, int client_fd,
                       struct fla_msg const * const recv,
                       struct fla_msg const * const send)
{
  // geo, dev_uri, md_uri
  char *write_ptr = send->data;
  size_t dev_uri_len, md_dev_uri_len;
  int err = 0;
  struct flexalloc *fs = daemon->flexalloc;

  memset(send->data, 0, FLA_MSG_DATA_MAX);
  memcpy(write_ptr, &fs->geo, sizeof(struct fla_geo));
  write_ptr += sizeof(struct fla_geo);

  dev_uri_len = strlen(fs->dev.dev_uri) + 1;
  memcpy(write_ptr, &dev_uri_len, sizeof(size_t));
  write_ptr += sizeof(size_t);
  memcpy(write_ptr, fs->dev.dev_uri, dev_uri_len);
  write_ptr += dev_uri_len;

  md_dev_uri_len = fs->dev.md_dev_uri ? strlen(fs->dev.md_dev_uri) : 0;
  memcpy(write_ptr, &md_dev_uri_len, sizeof(size_t));
  if(md_dev_uri_len > 0)
  {
    write_ptr += sizeof(size_t);
    memcpy(write_ptr, fs->dev.md_dev_uri, md_dev_uri_len);
  }

  send->hdr->len = sizeof(struct fla_geo)
                   + sizeof(size_t) + dev_uri_len + sizeof(size_t) + md_dev_uri_len;

  if (FLA_ERR(err = fla_sock_send_msg(client_fd, send), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_close_rq(struct flexalloc *fs)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);

  if (client->sock_fd == 0)
    return 0; /* ensure operation idempotency */

  client->send.hdr->cmd = FLA_MSG_CMD_SYNC_NO_RSPS;
  client->send.hdr->len = 0;
  err = fla_sock_send_msg(client->sock_fd, &client->send);
  if (FLA_ERR(err, "fla_sock_send_msg()"))
    return err;

  // while this should've been the LAST thing to do when reversing
  // the initialization order, we shut down the socket first to avoid
  // further requests or responses triggering processing.
  close(client->sock_fd);
  client->sock_fd = 0;

  fla_xne_dev_close(fs->dev.dev);
  fs->dev.dev = NULL;

  free(fs->pools.entries);
  fs->pools.entries = 0;

  free(fs->dev.dev_uri);
  fs->dev.dev_uri = NULL;

  fla_fs_free(client->flexalloc);
  return 0;
}

int
fla_daemon_close_rsp(struct fla_daemon *daemon, int client_fd,
                     struct fla_msg const * const recv,
                     struct fla_msg const * const send)
{
  int err;
  err = daemon->flexalloc->fns.close(daemon->flexalloc);
  *((int *)send->data) = err;

  send->hdr->len = sizeof(int);
  err = fla_sock_send_msg(client_fd, send);
  if (FLA_ERR(err, "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_sync_rq(struct flexalloc *fs)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);

  client->send.hdr->len = 0;
  client->send.hdr->cmd = FLA_MSG_CMD_SYNC;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    return err;

  return 0;
}

int
fla_daemon_sync_rsp(struct fla_daemon *daemon, int client_fd,
                    struct fla_msg const * const recv,
                    struct fla_msg const * const send)
{
  int err;
  err = daemon->flexalloc->fns.sync(daemon->flexalloc);

  if(send)
  {
    *((int *)send->data) = err;
    send->hdr->len = sizeof(int);
    err = fla_sock_send_msg(client_fd, send);
    if (FLA_ERR(err, "fla_sock_send_msg()"))
      goto exit;
  }

exit:
  return err;
}

int
fla_daemon_pool_open_rq(struct flexalloc *fs, char const *name, struct fla_pool **handle)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);
  struct fla_pool_entry *pool_entry = NULL;
  size_t name_len = strnlen((char *)name, FLA_NAME_SIZE_POOL);
  if (FLA_ERR(name_len == FLA_NAME_SIZE_POOL,
              "invalid, pool name exceeds max length or is not null-terminated"))
    return 1;

  (*handle) = malloc(sizeof(struct fla_pool));
  if (FLA_ERR(!(*handle), "malloc()"))
  {
    err = -ENOMEM;
    goto exit;
  }

  // write message to buffer
  memcpy(client->send.data, name, name_len);
  client->send.hdr->len = name_len;
  client->send.hdr->cmd = FLA_MSG_CMD_POOL_OPEN;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "pool_open()"))
    goto exit;


  // copy response from buffer to allocated structure
  memcpy(*handle, client->recv.data + sizeof(int), sizeof(struct fla_pool));
  pool_entry = &client->flexalloc->pools.entries[(*handle)->ndx];
  memcpy(pool_entry, client->recv.data + sizeof(int) + sizeof(struct fla_pool),
         sizeof(struct fla_pool_entry));

  return 0;

exit:
  if (err && *handle != NULL)
  {
    free(*handle);
    *handle = NULL;
  }
  return err;
}

int
fla_daemon_pool_open_rsp(struct fla_daemon *daemon, int client_fd,
                         struct fla_msg const * const recv,
                         struct fla_msg const * const send)
{
  int err;
  struct fla_pool **handle = NULL;
  struct fla_pool_entry *pool_entry = NULL;
  char *name = recv->data;
  size_t name_len = recv->hdr->len;
  name[name_len] = '\0'; // ensure string is null-terminated

  err = daemon->flexalloc->fns.pool_open(daemon->flexalloc, name, handle);
  *((int *)send->data) = err;
  send->hdr->len = sizeof(err);

  if (FLA_ERR(err, "pool_open()"))
  {
    send->hdr->len = sizeof(int);
  }
  else
  {
    // SUCCESS RSP: [<err code: 0>, <struct fla_pool>, <struct fla_pool_entry>]
    pool_entry = &daemon->flexalloc->pools.entries[(*handle)->ndx];
    send->hdr->len = sizeof(int) + sizeof(struct fla_pool) + sizeof(struct fla_pool_entry);
    memcpy(send->data + sizeof(int), *handle, sizeof(struct fla_pool));
    memcpy(send->data + sizeof(int) + sizeof(struct fla_pool), pool_entry,
           sizeof(struct fla_pool_entry));
  }

  if (FLA_ERR((err = fla_sock_send_msg(client_fd, send)), "fla_sock_send_msg()"))
    goto exit;

exit:
  if (handle !=NULL && *handle != NULL) // TODO: change API to not allocate the handle
    free(*handle);
  return err;
}

void
fla_daemon_pool_close_rq(struct flexalloc *fs, struct fla_pool *handle)
{
  // could do ref-counting on the daemon side if we sent a message back.
  free(handle);
}

int
fla_daemon_pool_create_rq(struct flexalloc *fs, char const *name, int name_len, uint32_t obj_nlb,
                          struct fla_pool **handle)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);
  struct fla_pool_entry * pool_entry;

  (*handle) = malloc(sizeof(struct fla_pool));
  if (FLA_ERR(!(*handle), "malloc()"))
  {
    err = -ENOMEM;
    goto exit;
  }

  // write message to buffer
  *((uint32_t *)client->send.data) = obj_nlb;
  memcpy(client->send.data + sizeof(obj_nlb), name, name_len);
  client->send.hdr->len = sizeof(uint32_t) + name_len;
  client->send.hdr->cmd = FLA_MSG_CMD_POOL_CREATE;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "pool_create()"))
    goto exit;

  // copy response from buffer to allocated structure
  memcpy(*handle, client->recv.data + sizeof(int), sizeof(struct fla_pool));
  pool_entry = &fs->pools.entries[(*handle)->ndx];
  memcpy(pool_entry, client->recv.data + sizeof(int) + sizeof(struct fla_pool),
         sizeof(struct fla_pool_entry));

exit:
  if (err && *handle != NULL)
  {
    free(*handle);
    *handle = NULL;
  }
  return err;
}

int
fla_daemon_pool_create_rsp(struct fla_daemon *daemon, int client_fd,
                           struct fla_msg const * const recv,
                           struct fla_msg const * const send)
{
  int err;
  uint32_t obj_nlb = *((uint32_t *)recv->data);
  char *name = (recv->data + sizeof(uint32_t));
  int name_len = recv->hdr->len - sizeof(uint32_t);
  struct fla_pool *handle = NULL;
  struct fla_pool_entry *pool_entry = NULL;

  err = daemon->flexalloc->fns.pool_create(daemon->flexalloc, name, name_len, obj_nlb, &handle);
  *((int *)send->data) = err;

  if (FLA_ERR(err, "pool_create()"))
  {
    send->hdr->len = sizeof(int);
    handle = NULL; //handle is undefined when there is an error
  }
  else
  {
    send->hdr->len = sizeof(int) + sizeof(struct fla_pool) + sizeof(struct fla_pool_entry);
    memcpy(send->data + sizeof(int), handle, sizeof(struct fla_pool));
    pool_entry = &daemon->flexalloc->pools.entries[handle->ndx];
    memcpy(send->data + sizeof(int) + sizeof(struct fla_pool), pool_entry,
           sizeof(struct fla_pool_entry));
  }

  if (FLA_ERR((err = fla_sock_send_msg(client_fd, send)), "fla_sock_send_msg()"))
    goto exit;

exit:
  if (handle != NULL) // TODO: change API to not allocate the handle
    free(handle);
  return err;
}

int
fla_daemon_pool_destroy_rq(struct flexalloc *fs, struct fla_pool *handle)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);
  memcpy(client->send.data, handle, sizeof(struct fla_pool));
  client->send.hdr->len = sizeof(struct fla_pool);
  client->send.hdr->cmd = FLA_MSG_CMD_POOL_DESTROY;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_sock_send_msg()"))
    goto exit;

  // did operation succeed ?
  err = *((int *)client->recv.data);
exit:
  return err;
}

int
fla_daemon_pool_destroy_rsp(struct fla_daemon *daemon, int client_fd,
                            struct fla_msg const * const recv,
                            struct fla_msg const * const send)
{
  int err;
  struct fla_pool *pool = NULL;

  if (FLA_ERR(recv->hdr->len != sizeof(struct fla_pool), "invalid message length"))
  {
    FLA_DBG_PRINTF("expected message to contain a 'struct fla_pool' (%zdB), but message length is %"PRIu32"B\n",
                   sizeof(struct fla_pool), recv->hdr->len);
    return 1;
  }

  // TODO: fla_base_pool_destroy also frees the handle - until changed, we have to allocate
  //       memory to subsequently free.
  // pool = (struct fla_pool*)recv->data;

  pool = malloc(sizeof(struct fla_pool));
  if (!pool)
  {
    err = -ENOMEM;
    goto exit;
  }

  memcpy(pool, recv->data, sizeof(struct fla_pool));


  err = daemon->flexalloc->fns.pool_destroy(daemon->flexalloc, pool);
  *((int *)send->data) = err;
  send->hdr->len = sizeof(int);

  if (FLA_ERR((err = fla_sock_send_msg(client_fd, send)), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_object_open_rq(struct flexalloc *fs, struct fla_pool *pool,
                          struct fla_object *object)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);

  memcpy(client->send.data, pool, sizeof(struct fla_pool));
  memcpy(client->send.data + sizeof(struct fla_pool), object, sizeof(struct fla_object));
  client->send.hdr->len = sizeof(struct fla_pool) + sizeof(struct fla_object);
  client->send.hdr->cmd = FLA_MSG_CMD_OBJECT_OPEN;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did the operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "object_open()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_object_open_rsp(struct fla_daemon *daemon, int client_fd,
                           struct fla_msg const * const recv,
                           struct fla_msg const * const send)
{
  int err;
  struct fla_pool *pool = (struct fla_pool*)recv->data;
  struct fla_object *object = (struct fla_object *) (recv->data + sizeof(struct fla_pool));

  err = daemon->flexalloc->fns.object_open(daemon->flexalloc, pool, object);
  if (FLA_ERR(err, "object_open()"))
  {
    FLA_DBG_PRINTF("failed to open object{slab_id: %"PRIu32", entry_ndx: %"PRIu32"}\n", object->slab_id,
                   object->entry_ndx);
  }

  *((int *)send->data) = err;
  send->hdr->len = sizeof(err);

  if (FLA_ERR((err = fla_sock_send_msg(client_fd, send)), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_object_create_rq(struct flexalloc *fs, struct fla_pool *pool,
                            struct fla_object *object)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);

  memcpy(client->send.data, pool, sizeof(struct fla_pool));
  client->send.hdr->len = sizeof(struct fla_pool);
  client->send.hdr->cmd = FLA_MSG_CMD_OBJECT_CREATE;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "object_create()"))
    goto exit;

  memcpy(object, client->recv.data + sizeof(int), sizeof(struct fla_object));

exit:
  return err;
}

int
fla_daemon_object_create_rsp(struct fla_daemon *daemon, int client_fd,
                             struct fla_msg const * const recv,
                             struct fla_msg const * const send)
{
  int err;
  struct fla_pool *pool = (struct fla_pool *)recv->data;
  struct fla_object object;

  err = daemon->flexalloc->fns.object_create(daemon->flexalloc, pool, &object);
  *((int *)send->data) = err;

  if (FLA_ERR(err, "object_create()"))
  {
    send->hdr->len = sizeof(int);
  }
  else
  {
    send->hdr->len = sizeof(int) + sizeof(struct fla_object);
    memcpy(send->data + sizeof(int), &object, sizeof(struct fla_object));
  }

  if (FLA_ERR(err = fla_sock_send_msg(client_fd, send), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_object_destroy_rq(struct flexalloc *fs, struct fla_pool *pool, struct fla_object *object)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);

  memcpy(client->send.data, pool, sizeof(struct fla_pool));
  memcpy(client->send.data + sizeof(struct fla_pool), object, sizeof(struct fla_object));
  client->send.hdr->len = sizeof(struct fla_pool) + sizeof(struct fla_object);
  client->send.hdr->cmd = FLA_MSG_CMD_OBJECT_DESTROY;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did the operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "object_destroy()"))
    goto exit;

exit:
  return err;
}


int
fla_daemon_object_destroy_rsp(struct fla_daemon *daemon, int client_fd,
                              struct fla_msg const * const recv,
                              struct fla_msg const * const send)
{
  int err;
  struct fla_pool *pool = (struct fla_pool *)recv->data;
  struct fla_object *object = (struct fla_object *)(recv->data + sizeof(struct fla_pool));

  err = daemon->flexalloc->fns.object_destroy(daemon->flexalloc, pool, object);
  if (FLA_ERR(err, "object_destroy()"))
  {} // nothing to do

  *((int *)send->data) = err;
  send->hdr->len = sizeof(err);

  if (FLA_ERR((err = fla_sock_send_msg(client_fd, send)), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_pool_set_root_object_rq(struct flexalloc const * const fs,
                                   struct fla_pool const * pool,
                                   struct fla_object const * object,
                                   fla_root_object_set_action action)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);
  char *data = client->send.data;

  memcpy(data, pool, sizeof(struct fla_pool));
  data += sizeof(struct fla_pool);
  memcpy(data, object, sizeof(struct fla_object));
  data += sizeof(struct fla_object);
  memcpy(data, &action, sizeof(action));

  client->send.hdr->len = sizeof(struct fla_pool) + sizeof(struct fla_object) + sizeof(action);
  client->send.hdr->cmd = FLA_MSG_CMD_POOL_SET_ROOT_OBJECT;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "pool_set_root_object()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_pool_set_root_object_rsp(struct fla_daemon *daemon, int client_fd,
                                    struct fla_msg const * const recv,
                                    struct fla_msg const * const send)
{
  int err;
  char *recv_data = recv->data;

  struct fla_pool *pool = (struct fla_pool*)recv_data;
  recv_data += sizeof(struct fla_pool);
  struct fla_object *object = (struct fla_object *)recv_data;
  recv_data += sizeof(struct fla_object);
  fla_root_object_set_action *action = (fla_root_object_set_action *)recv_data;

  err = daemon->flexalloc->fns.pool_set_root_object(daemon->flexalloc, pool, object, *action);
  *((int *)send->data) = err;
  send->hdr->len = sizeof(int);

  if (FLA_ERR(err = fla_sock_send_msg(client_fd, send), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

int
fla_daemon_pool_get_root_object_rq(struct flexalloc const * const fs,
                                   struct fla_pool const * pool,
                                   struct fla_object *object)
{
  int err;
  struct fla_daemon_client *client = fla_get_client(fs);

  memcpy(client->send.data, pool, sizeof(struct fla_pool));
  client->send.hdr->len = sizeof(struct fla_pool);
  client->send.hdr->cmd = FLA_MSG_CMD_POOL_GET_ROOT_OBJECT;

  err = fla_send_recv(client);
  if (FLA_ERR(err, "fla_send_recv()"))
    goto exit;

  // did operation succeed ?
  err = *((int *)client->recv.data);
  if (FLA_ERR(err, "pool_get_root_object()"))
    goto exit;

  memcpy(object, client->recv.data+sizeof(int), sizeof(struct fla_object));

exit:
  return err;
}

int
fla_daemon_pool_get_root_object_rsp(struct fla_daemon *daemon, int client_fd,
                                    struct fla_msg const * const recv,
                                    struct fla_msg const * const send)
{
  int err;
  struct fla_pool *pool = (struct fla_pool *)recv->data;
  struct fla_object object;

  err = daemon->flexalloc->fns.pool_get_root_object(daemon->flexalloc, pool, &object);
  if (FLA_ERR(err, "pool_get_root_object()"))
  {
    send->hdr->len = sizeof(int);
  }
  else
  {
    send->hdr->len = sizeof(int) + sizeof(struct fla_object);
    memcpy(send->data + sizeof(int), &object, sizeof(struct fla_object));
  }

  if (FLA_ERR(err = fla_sock_send_msg(client_fd, send), "fla_sock_send_msg()"))
    goto exit;

exit:
  return err;
}

struct fla_fns client_fns =
{
  .close = &fla_daemon_close_rq,
  .sync = &fla_daemon_sync_rq,
  .pool_open = &fla_daemon_pool_open_rq,
  .pool_close = &fla_daemon_pool_close_rq,
  .pool_create = &fla_daemon_pool_create_rq,
  .pool_destroy = &fla_daemon_pool_destroy_rq,
  .object_open = &fla_daemon_object_open_rq,
  .object_create = &fla_daemon_object_create_rq,
  .object_destroy = &fla_daemon_object_destroy_rq,
  .pool_set_root_object = &fla_daemon_pool_set_root_object_rq,
  .pool_get_root_object = &fla_daemon_pool_get_root_object_rq,
};

int
fla_daemon_open(const char *socket_path, struct fla_daemon_client *client)
{
  int err;
  struct sockaddr_un server;

  client->sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (FLA_ERR_ERRNO(err = client->sock_fd < 0, "socket()"))
    goto exit;

  if (FLA_ERR(err = fla_sock_sockaddr_init(&server, socket_path), "fla_sock_sockaddr_init()"))
    goto socket_close;

  if (FLA_ERR(err = connect(client->sock_fd, (struct sockaddr *)&server, sizeof(server)),
              "connect()"))
    goto socket_close;

  client->send.hdr = FLA_MSG_HDR(client->send_buf);
  client->send.data = FLA_MSG_DATA(client->send_buf);
  client->recv.hdr = FLA_MSG_HDR(client->recv_buf);
  client->recv.data = FLA_MSG_DATA(client->recv_buf);

  client->flexalloc = fla_fs_alloc();
  if (FLA_ERR(!client->flexalloc, "fla_fs_alloc()"))
    goto socket_close;

  // install back-pointer -- used by rq functions to get the client instance.
  client->flexalloc->user_data = client;

  FLA_DBG_PRINT("connected to server!!\n");
  if (FLA_ERR(fla_daemon_identify_rq(client, client->sock_fd, &client->server_version),
              "fla_daemon_identify_rq()"))
    goto free_flexalloc;

  FLA_DBG_PRINTF("identity{type: %"PRIu32", version: %"PRIu32"}\n", client->server_version.type,
                 client->server_version.version);

  // should be last step - if successful, device should be closed with fla_close
  err = fla_daemon_fs_init_rq(client, client->sock_fd);
  if (FLA_ERR(err, "fla_daemon_fs_init_rq()"))
    goto free_flexalloc;

  client->flexalloc->fns = client_fns;

  return err;

free_flexalloc:
  fla_fs_free(client->flexalloc);
  client->flexalloc = NULL;
socket_close:
  close(client->sock_fd);
  client->sock_fd = 0;
exit:
  return err;
}

