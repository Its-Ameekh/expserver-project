#include "../xps.h"

// Returns index of 'event' in loop->events among the first 'n_events' entries, -1 if not found
int loop_search_event(xps_loop_t *loop, loop_event_t *event, int n_events) {
  for (int i = 0; i < n_events; i++)
    if (loop->events.data[i] == event)
      return i;
  return -1;
}

loop_event_t *loop_event_create(u_int fd, void *ptr, xps_handler_t read_cb, xps_handler_t write_cb,
                                xps_handler_t close_cb) {
  assert(ptr != NULL);

  // Alloc memory for 'event' instance
  loop_event_t *event = malloc(sizeof(loop_event_t));
  if (event == NULL) {
    logger(LOG_ERROR, "event_create()", "malloc() failed for 'event'");
    return NULL;
  }

  event->fd = fd;
  event->ptr = ptr;
  event->read_cb = read_cb;
  event->write_cb = write_cb;
  event->close_cb = close_cb;

  logger(LOG_DEBUG, "event_create()", "created event");

  return event;
}

void loop_event_destroy(loop_event_t *event) {
  assert(event != NULL);

  free(event);

  logger(LOG_DEBUG, "event_destroy()", "destroyed event");
}

/**
 * Creates a new event loop instance associated with the given core.
 *
 * This function creates an epoll file descriptor, allocates memory for the xps_loop instance,
 * and initializes its values.
 *
 * @param core : The core instance to which the loop belongs
 * @return A pointer to the newly created loop instance, or NULL on failure.
 */
xps_loop_t *xps_loop_create(xps_core_t *core) {
  assert(core != NULL);

  // Create epoll instance
  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    logger(LOG_ERROR, "xps_loop_create()", "epoll_create1() failed");
    perror("Error message");
    return NULL;
  }

  // Alloc memory for 'loop' instance
  xps_loop_t *loop = malloc(sizeof(xps_loop_t));
  if (loop == NULL) {
    logger(LOG_ERROR, "xps_loop_create()", "malloc() failed for 'loop'");
    close(epoll_fd);
    return NULL;
  }

  // Init values
  loop->core = core;
  loop->epoll_fd = epoll_fd;
  vec_init(&(loop->events));
  loop->n_null_events = 0;

  logger(LOG_DEBUG, "xps_loop_create()", "created loop");

  return loop;
}

/**
 * Destroys the given loop instance and releases associated resources.
 *
 * This function destroys all loop_event_t instances present in loop->events list,
 * closes the epoll file descriptor and releases memory allocated for the loop instance,
 *
 * @param loop The loop instance to be destroyed.
 */
void xps_loop_destroy(xps_loop_t *loop) {
  assert(loop != NULL);

  // Destroy remaining events
  for (int i = 0; i < loop->events.length; i++) {
    loop_event_t *event = loop->events.data[i];
    if (event != NULL)
      loop_event_destroy(event);
  }
  vec_deinit(&(loop->events));

  close(loop->epoll_fd);
  free(loop);

  logger(LOG_DEBUG, "xps_loop_destroy()", "destroyed loop");
}

/**
 * Attaches a FD to be monitored using epoll
 *
 * The function creates an intance of loop_event_t and attaches it to epoll.
 * Add the pointer to loop_event_t to the events list in loop
 *
 * @param loop : loop to which FD should be attached
 * @param fd : FD to be attached to epoll
 * @param event_flags : epoll event flags
 * @param ptr : Pointer to instance of xps_listener_t or xps_connection_t
 * @param read_cb : Callback function to be called on a read event
 * @param write_cb : Callback function to be called on a write event
 * @param close_cb : Callback function to be called on a close event
 * @return : OK on success and E_FAIL on error
 */
int xps_loop_attach(xps_loop_t *loop, u_int fd, int event_flags, void *ptr, xps_handler_t read_cb,
                    xps_handler_t write_cb, xps_handler_t close_cb) {
  assert(loop != NULL);
  assert(ptr != NULL);

  loop_event_t *event = loop_event_create(fd, ptr, read_cb, write_cb, close_cb);
  if (event == NULL) {
    logger(LOG_ERROR, "xps_loop_attach()", "loop_event_create() failed");
    return E_FAIL;
  }

  // epoll gives back data.ptr, so we can reach the event directly
  struct epoll_event e;
  memset(&e, 0, sizeof(e));
  e.events = event_flags;
  e.data.ptr = event;

  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, fd, &e) < 0) {
    logger(LOG_ERROR, "xps_loop_attach()", "epoll_ctl() failed for fd %u", fd);
    perror("Error message");
    loop_event_destroy(event);
    return E_FAIL;
  }

  vec_push(&(loop->events), event);

  logger(LOG_DEBUG, "xps_loop_attach()", "attached fd %u to loop", fd);

  return OK;
}

/**
 * Remove FD from epoll
 *
 * Find the instance of loop_event_t from loop->events that matches fd param
 * and detach FD from epoll. Destroy the loop_event_t instance and set the pointer
 * to NULL in loop->events list. Increment loop->n_null_events.
 *
 * @param loop : loop instnace from which to detach fd
 * @param fd : FD to be detached
 * @return : OK on success and E_FAIL on error
 */
int xps_loop_detach(xps_loop_t *loop, u_int fd) {
  assert(loop != NULL);

  // Find the event for this fd
  int event_idx = -1;
  for (int i = 0; i < loop->events.length; i++) {
    loop_event_t *curr = loop->events.data[i];
    if (curr != NULL && curr->fd == fd) {
      event_idx = i;
      break;
    }
  }
  if (event_idx == -1) {
    logger(LOG_ERROR, "xps_loop_detach()", "no event found for fd %u", fd);
    return E_FAIL;
  }

  if (epoll_ctl(loop->epoll_fd, EPOLL_CTL_DEL, fd, NULL) < 0) {
    logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() failed for fd %u", fd);
    perror("Error message");
  }

  // Destroy the event and leave a NULL in its place
  loop_event_destroy(loop->events.data[event_idx]);
  loop->events.data[event_idx] = NULL;
  loop->n_null_events++;

  logger(LOG_DEBUG, "xps_loop_detach()", "detached fd %u from loop", fd);

  return OK;
}

void xps_loop_run(xps_loop_t *loop) {
  assert(loop != NULL);

  while (1) {
    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
    int n_events = epoll_wait(loop->epoll_fd, loop->epoll_events, MAX_EPOLL_EVENTS, -1);
    logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

    if (n_events < 0) {
      // Interrupted by a signal, just wait again
      if (errno == EINTR)
        continue;
      logger(LOG_ERROR, "xps_loop_run()", "epoll_wait() failed");
      perror("Error message");
      return;
    }

    logger(LOG_DEBUG, "xps_loop_run()", "handling %d events", n_events);

    // Events attached during this batch are pushed past this index. Only
    // search before it, so a stale event whose freed address got reused by
    // a new event is not mistaken for that new event.
    int n_old_events = loop->events.length;

    // Handle events
    for (int i = 0; i < n_events; i++) {
      logger(LOG_DEBUG, "xps_loop_run()", "handling event no. %d", i + 1);

      struct epoll_event curr_epoll_event = loop->epoll_events[i];
      loop_event_t *curr_event = curr_epoll_event.data.ptr;

      // Check if event still exists. Could have been destroyed due to prev event
      // 🟡 Below can be optimized using an RB tree
      if (loop_search_event(loop, curr_event, n_old_events) == -1) {
        logger(LOG_DEBUG, "handle_epoll_events()", "event not found. skipping");
        continue;
      }

      // Close event
      if (curr_epoll_event.events & (EPOLLERR | EPOLLHUP)) {
        logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / close");
        if (curr_event->close_cb != NULL)
          curr_event->close_cb(curr_event->ptr);
      }

      // Callback above may have destroyed the event
      if (loop_search_event(loop, curr_event, n_old_events) == -1)
        continue;

      // Read event
      if (curr_epoll_event.events & EPOLLIN) {
        logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / read");
        if (curr_event->read_cb != NULL)
          // Pass the ptr from loop_event_t as a parameter to the callback
          curr_event->read_cb(curr_event->ptr);
      }

      if (loop_search_event(loop, curr_event, n_old_events) == -1)
        continue;

      // Write event
      if (curr_epoll_event.events & EPOLLOUT) {
        logger(LOG_DEBUG, "handle_epoll_events()", "EVENT / write");
        if (curr_event->write_cb != NULL)
          curr_event->write_cb(curr_event->ptr);
      }
    }
  }
}
