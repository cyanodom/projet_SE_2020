#include <stdlib.h>
#include <stdio.h>

#include "load_conf.h"

int main(void) {
  size_t max_thread_nb;
  size_t min_thread_nb;
  size_t max_connect_per_thread;
  size_t shm_size;
  int load_file_ret = load_conf_file(&max_thread_nb, &min_thread_nb,
      &max_connect_per_thread, &shm_size);
  if (load_file_ret == LOAD_CONF_FAILURE) {
    return EXIT_FAILURE;
  }

  

  return EXIT_SUCCESS;
}
