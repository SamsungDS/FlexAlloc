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
#include "flexalloc_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fla_msg_header
{
  /// byte-length of the *data* segment of the message
  uint32_t len;
  /// the code identifying the type of command
  uint16_t cmd;
  /// a (optional) tag the client may use to correlate specific requests and replies.
  uint16_t tag;
};

struct fla_msg
{
  /// header-portion of the message buffer
  struct fla_msg_header *hdr;
  /// data-portion of the message buffer
  char *data;
};

#define FLA_MSG_CMD_NULL UINT16_MAX
#define FLA_MSG_CMD_IDENTIFY 1
#define FLA_MSG_CMD_SYNC 2

#define FLA_MSG_CMD_POOL_OPEN 3
#define FLA_MSG_CMD_POOL_CLOSE 4
#define FLA_MSG_CMD_POOL_CREATE 5
#define FLA_MSG_CMD_POOL_DESTROY 6
#define FLA_MSG_CMD_POOL_SET_ROOT_OBJECT 7
#define FLA_MSG_CMD_POOL_GET_ROOT_OBJECT 8

#define FLA_MSG_CMD_OBJECT_OPEN 9
#define FLA_MSG_CMD_OBJECT_CREATE 10
#define FLA_MSG_CMD_OBJECT_DESTROY 11
#define FLA_MSG_CMD_SYNC_NO_RSPS 12

#define FLA_MSG_CMD_INIT_INFO 30

struct fla_daemon;
typedef int (*fla_daemon_msg_handler_t)(struct fla_daemon *daemon, int client_fd,
                                        struct fla_msg const * const recv, struct fla_msg const * const send);

struct fla_sys_identity
{
  uint32_t type;
  uint32_t version;
};

#define FLA_SYS_FLEXALLOC_TYPE 1000
#define FLA_SYS_FLEXALLOC_V1 1

struct fla_daemon
{
  struct flexalloc *flexalloc;
  struct fla_sys_identity identity;
  int listen_fd;
  struct sockaddr_un server;
  int max_clients;
  fla_daemon_msg_handler_t on_msg;
};

// maximum amount of data in a message
#define FLA_MSG_DATA_MAX 2048
// message buffer size - protocol mandates all messages fit within a buffer this size
#define FLA_MSG_BUFSIZ (sizeof(struct fla_msg_header) + FLA_MSG_DATA_MAX)

/// get pointer to the message header struct
#define FLA_MSG_HDR(x) ((struct fla_msg_header *)*(&x))
/// get pointer to the beginning of the data
#define FLA_MSG_DATA(x) ( ((char *)(*(&x))) + sizeof(struct fla_msg_header) )

struct fla_daemon_client
{
  struct flexalloc *flexalloc;
  struct fla_sys_identity server_version;
  int sock_fd;
  /// view into send buffer
  struct fla_msg send;
  /// view into recv buffer
  struct fla_msg recv;
  /// packed message buffer
  char send_buf[FLA_MSG_BUFSIZ];
  /// packed message buffer
  char recv_buf[FLA_MSG_BUFSIZ];
};

struct fla_daemon_client *
fla_get_client(struct flexalloc const * const fs);

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
 * @return 0 on success, positive values are application-specific, negative
 *         values are negated errno codes.
 */
int
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
fla_sock_send_msg(int sock_fd, struct fla_msg const * const msg);

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
fla_sock_recv_msg(int sock_fd, struct fla_msg const * const msg);


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


int
fla_daemon_identify_rq(struct fla_daemon_client *client, int sock_fd,
                       struct fla_sys_identity *identity);

int
fla_daemon_identify_rsp(struct fla_daemon *daemon, int client_fd,
                        struct fla_msg const * const recv,
                        struct fla_msg const * const send);

int
fla_daemon_fs_init_rq(struct fla_daemon_client *client, int sock_fd);

int
fla_daemon_fs_init_rsp(struct fla_daemon *daemon, int client_fd,
                       struct fla_msg const * const recv,
                       struct fla_msg const * const send);

int
fla_daemon_close_rq(struct flexalloc *fs);

int
fla_daemon_close_rsp(struct fla_daemon *daemon, int client_fd,
                     struct fla_msg const * const recv,
                     struct fla_msg const * const send);

int
fla_daemon_sync_rq(struct flexalloc *fs);

int
fla_daemon_sync_rsp(struct fla_daemon *daemon, int client_fd,
                    struct fla_msg const * const recv,
                    struct fla_msg const * const send);

int
fla_daemon_pool_open_rq(struct flexalloc *fs, char const *name, struct fla_pool **handle);

int
fla_daemon_pool_open_rsp(struct fla_daemon *daemon, int client_fd,
                         struct fla_msg const * const recv,
                         struct fla_msg const * const send);

void
fla_daemon_pool_close_rq(struct flexalloc *fs, struct fla_pool *handle);

// no daemon_pool_rsp - no message is sent.

int
fla_daemon_pool_create_rq(struct flexalloc *fs, char const *name, int name_len, uint32_t obj_nlb,
                          struct fla_pool **handle);

int
fla_daemon_pool_create_rsp(struct fla_daemon *daemon, int client_fd,
                           struct fla_msg const * const recv,
                           struct fla_msg const * const send);

int
fla_daemon_pool_destroy_rq(struct flexalloc *fs, struct fla_pool *handle);

int
fla_daemon_pool_destroy_rsp(struct fla_daemon *daemon, int client_fd,
                            struct fla_msg const * const recv,
                            struct fla_msg const * const send);

int
fla_daemon_object_open_rq(struct flexalloc *fs, struct fla_pool *pool,
                          struct fla_object *object);

int
fla_daemon_object_open_rsp(struct fla_daemon *daemon, int client_fd,
                           struct fla_msg const * const recv,
                           struct fla_msg const * const send);

int
fla_daemon_object_create_rq(struct flexalloc *fs, struct fla_pool *pool,
                            struct fla_object *object);

int
fla_daemon_object_create_rsp(struct fla_daemon *daemon, int client_fd,
                             struct fla_msg const * const recv,
                             struct fla_msg const * const send);

int
fla_daemon_object_destroy_rq(struct flexalloc *fs, struct fla_pool *pool,
                             struct fla_object *object);

int
fla_daemon_object_destroy_rsp(struct fla_daemon *daemon, int client_fd,
                              struct fla_msg const * const recv,
                              struct fla_msg const * const send);

int
fla_daemon_pool_set_root_object_rq(struct flexalloc const * const fs,
                                   struct fla_pool const * pool,
                                   struct fla_object const * object,
                                   fla_root_object_set_action action);

int
fla_daemon_pool_set_root_object_rsp(struct fla_daemon *daemon, int client_fd,
                                    struct fla_msg const * const recv,
                                    struct fla_msg const * const send);

int
fla_daemon_pool_get_root_object_rq(struct flexalloc const * const fs,
                                   struct fla_pool const * pool,
                                   struct fla_object *object);

int
fla_daemon_pool_get_root_object_rsp(struct fla_daemon *daemon, int client_fd,
                                    struct fla_msg const * const recv,
                                    struct fla_msg const * const send);

int
fla_daemon_open(const char * socket_path, struct fla_daemon_client * client);

#ifdef __cplusplus
}
#endif

#endif // FLEXALLOC_DAEMON_BASE_H_
