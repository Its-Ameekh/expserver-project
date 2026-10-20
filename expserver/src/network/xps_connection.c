#include "../xps.h"

// Function declaration for read callback of connection
void connection_loop_read_handler(void *ptr);

xps_connection_t *xps_connection_create(xps_core_t *core, u_int sock_fd) {
  assert(core != NULL);

  xps_connection_t *connection = malloc(sizeof(xps_connection_t));
  if (connection == NULL) {
    logger(LOG_ERROR, "xps_connection_create()", "malloc() failed for 'connection'");
    return NULL;
  }

  // Attach sock_fd to epoll
  if (xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection, connection_loop_read_handler) != OK) {
    logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
    free(connection);
    return NULL;
  }

  // Init values
  connection->core = core;
  connection->sock_fd = sock_fd;
  connection->listener = NULL;
  connection->remote_ip = get_remote_ip(sock_fd);

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

  // Free inner heap memory FIRST, then the struct
  free(connection->remote_ip);
  free(connection);

  logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

// Function definition for read callback of connection
void connection_loop_read_handler(void *ptr) {

  // Validate params
  assert(ptr != NULL);
  xps_connection_t *connection = ptr;

  // static -> 100KB lives in data segment, not on the stack
  static u_char buff[DEFAULT_BUFFER_SIZE];

  // Read data from client (leave 1 byte for '\0')
  long read_n = recv(connection->sock_fd, buff, sizeof(buff) - 1, 0);

  if (read_n < 0) {
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

  // Print client message
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

  // Sending reversed message to client (loop handles partial sends)
  long bytes_written = 0;
  long message_len = read_n;
  while (bytes_written < message_len) {
    // MSG_NOSIGNAL -> a closed peer gives EPIPE instead of killing the server
    long write_n = send(connection->sock_fd, buff + bytes_written, message_len - bytes_written, MSG_NOSIGNAL);
    if (write_n <= 0) {
      logger(LOG_ERROR, "connection_read_handler()", "send() failed");
      perror("Error message");
      xps_connection_destroy(connection);
      return;
    }
    bytes_written += write_n;
  }
}
