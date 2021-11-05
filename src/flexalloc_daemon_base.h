/**
 * Flexalloc daemon API.
 *
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 *
 * @file flexalloc_daemon_base.h
 */
#ifndef FLEXALLOC_DAEMON_BASE_H_
#define FLEXALLOC_DAEMON_BASE_H_
#include <stdint.h>
#include <sys/un.h>
#include <sys/types.h>
#include <signal.h>
#include "flexalloc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct msg_header
{
  uint32_t len;
  uint32_t tag;
};

#define FLA_MSG_TAG_IDENTIFY_RQ 1
#define FLA_MSG_TAG_IDENTIFY_RSP 2

struct fla_daemon;
typedef int (*fla_daemon_msg_handler_t)(struct fla_daemon *daemon, int client_fd,
                                        struct msg_header *hdr, char *msg_buf);

struct fla_daemon
{
  int listen_fd;
  struct sockaddr_un server;
  int max_clients;
  fla_daemon_msg_handler_t on_msg;
};

// maximum amount of data in a message
#define FLA_MSG_DATA_MAX 2048
// message buffer size - protocol mandates all messages fit within a buffer this size
#define FLA_MSG_BUFSIZ (sizeof(struct msg_header) + FLA_MSG_DATA_MAX)

/// get pointer to the message header struct
#define FLA_MSG_HDR(x) ((struct msg_header *)*(&x))
/// get pointer to the beginning of the data
#define FLA_MSG_DATA(x) ( ((char *)(*(&x))) + sizeof(struct msg_header) )

struct fla_daemon_client
{
  struct flexalloc base;
  int sock_fd;
  /// packed message buffer
  char snd_buf[FLA_MSG_BUFSIZ];
  /// packed message buffer
  char rcv_buf[FLA_MSG_BUFSIZ];
};


int
fla_max_open_files();

/**
 * Create flexalloc daemon instance
 * @param d the daemon
 * @param socket_path where to place the UNIX socket
 * @param on_msg the handler which should parse messages received from the client
 * @param max_clients maximum number of concurrent clients to support
 * @param conn_queue_length maximum length of queue to buffer pending connection requests.
 */
int
fla_daemon_create(struct fla_daemon *d, char *socket_path, fla_daemon_msg_handler_t on_msg,
                  int max_clients, int conn_queue_length);

/**
 * Destroy daemon.
 *
 * Call as part of the daemon shut down to release the UNIX socket etc.
 * @param d the (maybe initialized) daemon context
 *
 * @return On success 0, -1 if closing the server socket fails, -2 if removing the socket file fails.
 */
int
fla_daemon_destroy(struct fla_daemon *d);

/**
 * Send n bytes of data from buffer buf over provided socket.
 *
 * @param sock_fd the socket file descriptor
 * @param buf the data buffer
 * @param n the number of bytes to send.
 *
 * @return number of bytes sent - thus success is when the return value matches n.
 */
ssize_t
fla_sock_send_bytes(int sock_fd, char *buf, size_t n);

/**
 * Send message to recipient over socket identified by sock_fd.
 *
 * Note: A message is a byte array organized as
 * [<op code: uint32_t>, <op length: uint32_t>, <data>], and the 'op code'
 * value corresponds to the exact byte length of the 'data' byte array.
 *
 * @param sock_fd the socket file descriptor of the receiving party
 * @param msg the buffer containing the message to send
 *
 * @return on success 0, -1 otherwise.
 */
int
fla_sock_send_msg(int sock_fd, char *msg);

/**
 * Receive message from socket identified by sock_fd.
 *
 * Note: A message is a byte array organized as
 * [<op code: uint32_t>, <op length: uint32_t>, <data>], and the 'op code'
 * value corresponds to the exact byte length of the 'data' byte array.
 *
 * @param sock_fd the socket file descriptor or the sender
 * @param msg the message buffer to receive the message contents
 *
 * @return on success 0, -1 otherwise.
 */
int
fla_sock_recv_msg(int sock_fd, char *msg);


/**
 * Start the daemon server loop.
 *
 * Starts the daemon server loop which handles incoming connections, reads incoming
 * messages and dispatches them to the provided handler function for processing.
 *
 * @param d the daemon handler
 * @param keep_running pointer to a variable indicating whether to continue the server loop,
 *        ideally configure a SIG-INT handler to set keep-value to zero, causing the server
 *        to gracefully exit the processing loop.
 * @return 0 on success, non-zero in case of an unrecoverable error.
 */
int
fla_daemon_loop(struct fla_daemon *d,
                volatile sig_atomic_t *keep_running);

#ifdef __cplusplus
}
#endif

#endif // FLEXALLOC_DAEMON_BASE_H_
