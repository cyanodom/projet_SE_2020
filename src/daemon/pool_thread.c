#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>

#include "pool_thread.h"
#include "macros.h"

volatile size_t nb_threads;
volatile bool *thread_used;

static void *pool_thread__run(void *param);
static int pool_thread__create_n(pthread_t *threads[], size_t max_thread_nb,
    size_t nb);
static int pool_thread__create(pthread_t *threads[], size_t max_thread_nb);

//remplis le tableau threads de taille supposée max_thread_nb avec les id des
//threads ou NULL si aucun thread n'est associé
int pool_thread_init(pthread_t **threads, size_t min_thread_nb,
    size_t max_thread_nb) {
  nb_threads = 0;
  thread_used = malloc(sizeof(bool) * max_thread_nb);

  size_t i;
  for (i = 0; i < min_thread_nb; ++i) {
    threads[i] = malloc(sizeof(pthread_t));
    thread_used[i] = false;
    if (threads[i] == NULL) {
      PRINT_ERR("%s : %s : %s", "pool_thread_init", "malloc",
          strerror(errno));
      return POOL_THREAD_FAILURE;
    }

    int err =
        pthread_create(threads[i], NULL, pool_thread__run, &max_thread_nb);
    if (err != 0) {
      PRINT_ERR("%s : %s", "pool_thread_init", strerror(err));
      threads[i] = NULL;
      return POOL_THREAD_FAILURE;
    }
    ++nb_threads;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_manage(pthread_t *threads[], size_t min_thread_nb) {
  long int thread_lack = (long int) min_thread_nb - (long int) nb_threads;
  if (thread_lack > 0) {
    //min_thread_nb est correct puisque nous savons que max_thread_nb >=
    //min_thread_nb est nb_threads ne peut que diminuer sous l'action des thread
    if (pool_thread__create_n(threads, min_thread_nb, (size_t) thread_lack)
        == POOL_THREAD_FAILURE) {
      return POOL_THREAD_FAILURE;
    }
    nb_threads += (size_t) thread_lack;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_enroll_new_thread(pthread_t *threads, size_t max_thread_nb,
    int *fd) {
  if (nb_threads > max_thread_nb) {
    return POOL_TRHEAD_NO_MORE_THREAD;
  }
  size_t index = 0;
  while (threads[index] != NULL) {
    if (!thread_used[i]) {//si le thread est libre
      thread_used[i] = true;
    }
    ++index;
  }
}

void pool_thread_dispose(pthread_t **threads) {
  size_t freeId = 0;
  for (size_t i = 0; i < nb_threads; ++i) {
    while (threads[freeId] == NULL) {
      ++freeId;
    }
    //TODO
  }
  *threads = NULL;
}

void *pool_thread__run(void * param) {
  size_t __attribute__((unused)) max_thread_nb = *(size_t *)param ;
  switch(fork()) {
    case 0:
      printf("thread : %zu\n", nb_threads);
      break;
    default:
      break;
  }
  return NULL;
}

int pool_thread__create(pthread_t *threads[], size_t max_thread_nb) {
  if (nb_threads < max_thread_nb) {
    size_t index = 0;
    while (threads[index] != NULL) {
      ++index;
    }

    threads[index] = malloc(sizeof(pthread_t));
    if (threads[index] == NULL) {
      PRINT_ERR("%s : %s : %s", "pool_thread__create", "malloc",
          strerror(errno));
      return POOL_THREAD_FAILURE;
    }

    int err =
        pthread_create(threads[index], NULL, pool_thread__run, &max_thread_nb);
    if (err != 0) {
      PRINT_ERR("%s : %s", "pool_thread__create", strerror(err));
      threads[index] = NULL;
      return POOL_THREAD_FAILURE;
    }
    ++nb_threads;
    return POOL_THREAD_SUCCESS;
  }
  return POOL_TRHEAD_NO_MORE_THREAD;
}

int pool_thread__create_n(pthread_t *threads[], size_t max_thread_nb,
    size_t nb) {
  size_t index = 0;
  for (size_t i = 0; i < nb; ++i) {
    if (nb_threads < max_thread_nb) {
      while (threads[index] != NULL) {
        ++index;
      }

      threads[index] = malloc(sizeof(pthread_t));
      if (threads[index] == NULL) {
        PRINT_ERR("%s : %s : %s", "pool_thread__create_n", "malloc",
            strerror(errno));
        return POOL_THREAD_FAILURE;
      }

      int err =
          pthread_create(threads[index], NULL, pool_thread__run, &max_thread_nb);
      if (err != 0) {
        PRINT_ERR("%s : %s", "pool_thread__create_n", strerror(err));
        threads[index] = NULL;
        return POOL_THREAD_FAILURE;
      }
      ++nb_threads;
    }
    return POOL_TRHEAD_NO_MORE_THREAD;
  }
  return POOL_THREAD_SUCCESS;
}
