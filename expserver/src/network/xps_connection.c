#include "../xps.h"

// Max bytes of a received chunk printed to stdout
#define MAX_PRINT_LEN 1024

// Function declarations for callbacks of connection
void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_read_handler(void *ptr);
void connection_write_handler(void *ptr);

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {
  assert(core != NULL);

  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return NULL;
  }

  // Buffer list for data waiting to be sent to the client
  xps_buffer_list_t *write_buff_list = xps_buffer_list_create();
  if (write_buff_list == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_buffer_list_create() failed");
    free(connection);
    return NULL;
  }

  // Attach sock_fd to epoll for read and write events
  // Edge triggered: epoll notifies only when the socket's state changes
  if (xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection, connection_loop_read_handler,
                      connection_loop_write_handler, connection_loop_close_handler) != OK) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
    xps_buffer_list_destroy(write_buff_list);
    free(connection);
    return NULL;
  }

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);
  connection->write_buff_list = write_buff_list;
  connection->read_ready = false;
  connection->write_ready = false;
  connection->send_handler = connection_write_handler;
  connection->recv_handler = connection_read_handler;

  // Add connection to core's 'connections' list
  vec_push(&(core->connections), connection);

  logger(LOG_DEBUG, "xps_connection_create()", "created connection");
  return connection;
}

void xps_connection_destroy(xps_connection_t *connection) {

  // Validate params
  assert(connection != NULL);

  // Set connection to NULL in 'connections' list
  xps_core_t *core = connection->core;
  for (int i = 0; i < core->connections.length; i++) {
    xps_connection_t *curr = core->connections.data[i];
    if (curr == connection) {
      core->connections.data[i] = NULL;
      core->n_null_connections++;
      break;
    }
  }

  // Detach connection from loop
  xps_loop_detach(core->loop, connection->sock_fd);

  // Close connection socket FD
  close(connection->sock_fd);

  // Free inner heap memory FIRST (including unsent data), then the struct
  xps_buffer_list_destroy(connection->write_buff_list);
  free(connection->remote_ip);
  free(connection);

  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

// Epoll read callback: only marks the connection as ready to read
void connection_loop_read_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  connection->read_ready = true;
}

// Epoll write callback: only marks the connection as ready to write
void connection_loop_write_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  connection->write_ready = true;
}

// Receives data from the client, called by the loop while read_ready is true
void connection_read_handler(void *ptr) {

  // Validate params
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  // static -> 100KB lives in data segment, not on the stack
  static u_char buff[DEFAULT_BUFFER_SIZE];

  // Read data from client (leave 1 byte for '\0')
  long read_n = recv(connection->sock_fd, buff, sizeof(buff) - 1, 0);

  if (read_n < 0) {
    // Socket fully drained, wait for the next read event from epoll
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      connection->read_ready = false;
      return;
    }
    logger(LOG_ERROR, "connection_read_handler()", "recv() failed");
    perror("Error message");
    xps_connection_destroy(connection);
    return;
  }

  if (read_n == 0) {
    logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
    xps_connection_destroy(connection);
    return;
  }

  buff[read_n] = '\0';

  // Print client message (only the start of big chunks, so huge uploads don't flood the logs)
  if (read_n > MAX_PRINT_LEN)
    printf("[CLIENT MESSAGE] %.*s...\n", MAX_PRINT_LEN, buff);
  else
    printf("[CLIENT MESSAGE] %s", buff);
  fflush(stdout);

  // Reverse client message (keep trailing newline from netcat at the end)
  long len = read_n;
  if (len > 0 && buff[len - 1] == '\n')
    len--;
  for (long start = 0, end = len - 1; start < end; start++, end--) {
    u_char temp = buff[start];
    buff[start] = buff[end];
    buff[end] = temp;
  }

  // Copy reversed message into a buffer and queue it for sending
  xps_buffer_t *reversed = xps_buffer_create(read_n, read_n, NULL);
  if (reversed == NULL) {
    logger(LOG_ERROR, "connection_read_handler()", "xps_buffer_create() failed");
    xps_connection_destroy(connection);
    return;
  }
  memcpy(reversed->data, buff, read_n);
  xps_buffer_list_append(connection->write_buff_list, reversed);
}

// Sends buffered data to the client, called by the loop while write_ready is true
void connection_write_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  // Nothing waiting to be sent
  if (connection->write_buff_list->len == 0)
    return;

  // Read all pending data from the buffer list
  xps_buffer_t *buff = xps_buffer_list_read(connection->write_buff_list, connection->write_buff_list->len);
  if (buff == NULL) {
    logger(LOG_ERROR, "connection_write_handler()", "xps_buffer_list_read() failed");
    return;
  }

  // MSG_NOSIGNAL -> a closed peer gives EPIPE instead of killing the server
  long write_n = send(connection->sock_fd, buff->data, buff->len, MSG_NOSIGNAL);
  xps_buffer_destroy(buff);

  if (write_n < 0) {
    // Kernel buffer is full, keep the data and wait for the next write event
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      connection->write_ready = false;
      return;
    }
    logger(LOG_ERROR, "connection_write_handler()", "send() failed");
    perror("Error message");
    xps_connection_destroy(connection);
    return;
  }

  if (write_n == 0) {
    logger(LOG_ERROR, "connection_write_handler()", "send() wrote 0 bytes");
    xps_connection_destroy(connection);
    return;
  }

  // Remove only the bytes that were actually sent (handles partial sends)
  xps_buffer_list_clear(connection->write_buff_list, write_n);
}

// Function definition for close callback of connection
void connection_loop_close_handler(void *ptr) {
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  logger(LOG_INFO, "connection_close_handler()", "peer closed connection");
  xps_connection_destroy(connection);
}
