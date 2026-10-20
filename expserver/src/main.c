#include "xps.h"

xps_core_t *core;

void sigint_handler(int signum);

int main() {
  signal(SIGINT, sigint_handler);

  core = xps_core_create();
  if (core == NULL) {
    logger(LOG_ERROR, "main()", "xps_core_create() failed");
    exit(EXIT_FAILURE);
  }

  xps_core_start(core);

  return EXIT_SUCCESS;
}

void sigint_handler(int signum) {
  logger(LOG_WARNING, "sigint_handler()", "SIGINT received");

  // Destroying core destroys all listeners, connections and the loop
  xps_core_destroy(core);

  exit(EXIT_SUCCESS);
}
