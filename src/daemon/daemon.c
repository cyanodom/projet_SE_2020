#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "load_conf.h"
#include "pool_thread.h"
#include "pipe.h"

int main(void) {
  size_t max_thread_nb;
  size_t min_thread_nb;
  size_t max_connect_per_thread;
  size_t shm_size;
  int load_conf_ret = load_conf_file(&max_thread_nb, &min_thread_nb,
      &max_connect_per_thread, &shm_size);

  pthread_t threads[max_thread_nb];
  if (load_conf_ret == LOAD_CONF_FAILURE) {
    goto error;
  }

  int pipefd;
  if (pipe_create_base(&pipefd) == PIPE_FAILURE) {
    goto error;
  }

  int pool_thread_ret = pool_thread_init((pthread_t **)&threads, min_thread_nb,
      max_thread_nb);
  if (pool_thread_ret == POOL_THREAD_FAILURE) {
    goto error;
  }

  bool error;
  goto dispose;
error:
  error = true;
  goto error_dispose;
dispose:
  error = false;
error_dispose:
  if (pipe_dispose(pipefd) == PIPE_FAILURE) {
    return EXIT_FAILURE;
  }

  if (error) {
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
