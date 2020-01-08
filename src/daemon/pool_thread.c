#include <stdlib.h>
#include <pthread.h>

#include "pool_thread.h"

static void *run(void *param);

int pool_create(pthread_t *threads, size_t min_thread_nb,
    size_t max_thread_nb) {
  threads = malloc(sizeof(pthread_t) * max_thread_nb);
  if (threads == NULL) {
    free(threads);
    return POOL_CREATE_FAILURE;
  }
  for (size_t i = 0; i < min_thread_nb; ++i) {
    pthread_create(&threads[i], NULL, run, &max_thread_nb);

  }
  return POOL_CREATE_SUCCESS;
}

void *run(void * param) {
  max_thread_nb = *param;
  //TODO
  return NULL;
}
