#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "macros.h"
#include "pool_thread.h"

#define STR_PREFIX_THREAD " + "
#define STR_THREAD_WORK STR_PREFIX_THREAD "Un thread travaille"
#define STR_THREAD_WAIT STR_PREFIX_THREAD "Un thread attends du travail"
#define STR_THREAD_DONE STR_PREFIX_THREAD "Un thread a fini son travail"
#define STR_THREAD_DEAD STR_PREFIX_THREAD "Un thread s'est détruit"


#define FD_KILL -1
#define BASE_SHM_NAME "shm_thread_"

static size_t max_connect;

//Everytime global_nb_threads is very near the real number of threads
static size_t global_nb_threads;
static sem_t sem_mutex_nb;

static int global_shm_fd;
static char global_shm_name[WORD_LEN_MAX];
static sem_t sem_thread_get;
static sem_t sem_daemon_send;
static sem_t sem_mutex_shm;

struct pool_thread {
  pthread_t **threads;
  sem_t *sem_thread_work;
  size_t max_thread_nb;
  size_t min_thread_nb;
  size_t shm_size;
};

static void *pool_thread__run(void *param);
static int pool_thread__shm_name_open(char *name, int *shm_fd);
static int pool_thread__send_shm(pool_thread *pool, char *shm_name, int fd,
    size_t index_thread);
static int pool_thread__create_thread(pool_thread *pool, size_t i);
static int pool_thread__where_can_create_thread(pool_thread *pool,
    size_t *index_thread);

//remplis le tableau threads de taille supposée max_thread_nb avec les id des
//threads ou NULL si aucun thread n'est associé
int pool_thread_init(pool_thread **pool, size_t min_thread_nb,
    size_t max_thread_nb, size_t max_connect_per_thread, size_t shm_size) {
  global_nb_threads = 0;
  global_shm_name[0] = 0;
  global_shm_fd = FD_KILL;
  max_connect = max_connect_per_thread;
  //create pool and threads
  *pool = malloc(sizeof(pool_thread));
  if (*pool == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  (*pool)->min_thread_nb = min_thread_nb;
  (*pool)->max_thread_nb = max_thread_nb;
  (*pool)->shm_size = shm_size;
  (*pool)->threads = malloc(sizeof(pthread_t) * max_thread_nb);
  if ((*pool)->threads == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  // create shm
  (*pool)->sem_thread_work = malloc(sizeof(sem_t) * max_thread_nb);
  if ((*pool)->sem_thread_work == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(&sem_mutex_nb, 0, 1) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(&sem_thread_get, 0, 1) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(&sem_daemon_send, 0, 0) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(&sem_mutex_shm, 0, 1) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  //launch threads
  size_t i;
  for (i = 0; i < min_thread_nb; ++i) {
    int pool_thread__create_thread_r = pool_thread__create_thread(*pool, i);
    if (pool_thread__create_thread_r != POOL_THREAD_SUCCESS) {
      return POOL_THREAD_FAILURE;
    }
  }

  //set the ohter threads to NULL
  while (i < max_thread_nb) {
    (*pool)->threads[i] = NULL;
    ++i;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_dispose(pool_thread **pool) {
  if (pool == NULL) return POOL_THREAD_SUCCESS;
  for (size_t i = 0; i < (*pool)->max_thread_nb; ++i) {
    if ((*pool)->threads[i] != NULL) {
      //tell the thread to kill itself
      int pool_thread__send_shm_r = pool_thread__send_shm(*pool, "\0",
          FD_KILL, i);
      if (pool_thread__send_shm_r != POOL_THREAD_SUCCESS) {
        PRINT_ERR("%s : %d", "pool_thread__send_shm", pool_thread__send_shm_r);
        return POOL_THREAD_FAILURE;
      }
      //free the thread state
      int pthread_join_r = pthread_join(*((*pool)->threads[i]), NULL);
      if (pthread_join_r != 0) {
        PRINT_ERR("%s : %s", "pthread_join", strerror(pthread_join_r));
        return POOL_THREAD_FAILURE;
      }
      free((*pool)->threads[i]);
    }
  }
  free((*pool)->sem_thread_work);
  free((*pool)->threads);
  free(*pool);
  *pool = NULL;

  sem_close(&sem_mutex_nb);
  sem_close(&sem_thread_get);
  sem_close(&sem_daemon_send);
  sem_close(&sem_mutex_shm);
  return POOL_THREAD_SUCCESS;
}

int pool_thread_enroll(pool_thread *pool, char *shm_name) {
  //find a free thread
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (pool->threads[i] != NULL) {
      int sem_value = 0;
      if (sem_getvalue(&pool->sem_thread_work[i], &sem_value) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_FAILURE;
      }
      if (sem_value == 0) {
        // this thread is free
        int shm_fd;
        int pool_thread__shm_name_open_r = pool_thread__shm_name_open(shm_name,
            &shm_fd);
        if (pool_thread__shm_name_open_r != POOL_THREAD_SUCCESS) {
          PRINT_ERR("%s : %d", "pool_thread__shm_name_open",
              pool_thread__shm_name_open_r);
              return POOL_THREAD_FAILURE;
        }
        printf("send\n");
        int pool_thread__send_shm_r = pool_thread__send_shm(pool, shm_name,
            shm_fd, i);
            printf("sent\n");
        if (pool_thread__send_shm_r != POOL_THREAD_SUCCESS) {
          PRINT_ERR("%s : %d", "pool_thread__send_shm",
              pool_thread__send_shm_r);
          return POOL_THREAD_FAILURE;
        }
        return POOL_THREAD_SUCCESS;
      }
    }
  }
  //there's no available threads, find a place to create a new one
  size_t index_thread;
  if (pool_thread__where_can_create_thread(pool, &index_thread)) {
    return POOL_THREAD_FAILURE;
  }
  //create it
  int pool_thread__create_thread_r =
      pool_thread__create_thread(pool, index_thread);
  if (pool_thread__create_thread_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %d", "pool_thread__create_thread",
        pool_thread__create_thread_r);
    return POOL_THREAD_FAILURE;
  }
  //and create and send shm
  int shm_fd;
  int pool_thread__shm_name_open_r = pool_thread__shm_name_open(shm_name,
      &shm_fd);
  if (pool_thread__shm_name_open_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %d", "pool_thread__shm_name_open",
        pool_thread__shm_name_open_r);
        return POOL_THREAD_FAILURE;
  }
  int pool_thread__send_shm_r = pool_thread__send_shm(pool, shm_name,
      shm_fd, index_thread);
  if (pool_thread__send_shm_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %d", "pool_thread__send_shm",
        pool_thread__send_shm_r);
    return POOL_THREAD_FAILURE;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_manage(pool_thread *pool) {
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (pool->threads[i] != NULL) {
      if (&pool->sem_thread_work[i] == NULL) {
        if (pthread_join(*(pool->threads[i]), NULL) != 0) {
          PRINT_ERR("%s : %s", "pthread_join", strerror(errno));
          return POOL_THREAD_FAILURE;
        }
      }
    }
  }
  if (sem_wait(&sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  size_t nb_threads = global_nb_threads;
  if (sem_post(&sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  while (nb_threads < pool->min_thread_nb) {
    size_t index_thread;
    int pool_thread__where_can_create_thread_r =
        pool_thread__where_can_create_thread(pool, &index_thread);
    if (pool_thread__where_can_create_thread_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %d", "pool_thread__where_can_create_thread",
          pool_thread__where_can_create_thread_r);
      return POOL_THREAD_FAILURE;
    }
    int pool_thread__create_thread_r =
        pool_thread__create_thread(pool, index_thread);
    if (pool_thread__create_thread_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %d", "pool_thread__create_thread",
          pool_thread__create_thread_r);
      return POOL_THREAD_FAILURE;
    }
    ++nb_threads;
  }

  return POOL_THREAD_SUCCESS;
}
//__________________________POOL_THREAD_TOOLS____________________________

void *pool_thread__run(void *param) {
  sem_t *sem_work = (sem_t *) param;
  char shm_name[WORD_LEN_MAX];
  int shm_fd;
  size_t remaining_work_plus_one = max_connect + 1;
  //wait to increment global_nb_threads

  if (sem_wait(&sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    exit(EXIT_FAILURE);
  }
  ++global_nb_threads;
  if (sem_post(&sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    exit(EXIT_FAILURE);
  }
  do {
    //wait to work
    PRINT_INFO("%s", STR_THREAD_WAIT);
    if (sem_wait(sem_work) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      exit(EXIT_FAILURE);
    }
    // now I work, wait to get shm fd
    if (sem_wait(&sem_daemon_send) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      exit(EXIT_FAILURE);
    }
    size_t index_str = 0;
    if (global_shm_name[0] != 0) {
      do {
        shm_name[index_str] = global_shm_name[index_str];
        ++index_str;
      } while (global_shm_name[index_str] != 0);
    }
    shm_name[index_str] = 0;
    shm_fd = global_shm_fd;
    if (sem_post(&sem_thread_get) == -1) {
      PRINT_ERR("%s : %s", "sem_post", strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (shm_fd != FD_KILL) {
      PRINT_INFO("%s", STR_THREAD_WORK);
      //TODO fork + shm
      sleep(3);

      PRINT_INFO("%s", STR_THREAD_DONE);
      //finish work by closing shm
      if (shm_unlink(shm_name) != 0) {
        PRINT_ERR("%s : %s", "shm_unlink", strerror(errno));
        exit(EXIT_FAILURE);
      }
      if (close(shm_fd) != 0) {
        PRINT_ERR("%s : %s", "close", strerror(errno));
        exit(EXIT_FAILURE);
      }
    }
    if (remaining_work_plus_one != 0
        && remaining_work_plus_one != 1) {
           --remaining_work_plus_one;
    }
  } while (shm_fd != FD_KILL && remaining_work_plus_one != 1);

  //thread need to die
  //wait to decrement global_nb_threads
  if (sem_wait(&sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    //send to me a signal to dispose
    //TODO
    exit(EXIT_FAILURE);
  }
  --global_nb_threads;
  if (sem_post(&sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    //send to me a signal to dispose
    //TODO
    exit(EXIT_FAILURE);
  }
  sem_destroy(sem_work);
  sem_work = NULL;
  PRINT_INFO("%s", STR_THREAD_DEAD);
  pthread_exit(NULL);
}

int pool_thread__send_shm(pool_thread *pool, char *shm_name, int fd,
    size_t index_thread) {
  if (sem_wait(&sem_thread_get) != 0) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_wait(&sem_mutex_shm) != 0) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  global_shm_fd = fd;
  //copy the shm_name into global_shm_name
  size_t i = 0;
  if (shm_name[0] != 0) {
    do {
      global_shm_name[i] = shm_name[i];
      ++i;
    } while (shm_name[i] != 0);
  }
  global_shm_name[i] = 0;
  if (sem_post(&sem_mutex_shm) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_post(&pool->sem_thread_work[index_thread]) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_post(&sem_daemon_send) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread__create_thread(pool_thread *pool, size_t i) {
  if (sem_init(&pool->sem_thread_work[i], 0, 0) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  pool->threads[i] = malloc(sizeof(pthread_t));
  if (pool->threads[i] == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  int err = pthread_create(pool->threads[i], NULL, pool_thread__run,
        &pool->sem_thread_work[i]);

  if (err != 0) {
    PRINT_ERR("%s : %s", "pthread_create", strerror(err));
    free(pool->threads[i]);
    pool->threads[i] = NULL;
    return POOL_THREAD_FAILURE;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread__where_can_create_thread(pool_thread *pool,
    size_t *index_thread) {
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (pool->threads[i] == NULL) {
      *index_thread = i;
      return POOL_THREAD_SUCCESS;
    }
  }
  return POOL_TRHEAD_NO_MORE_THREAD;
}

int pool_thread__generate_shm_name(char *shm_name, size_t id) {
  char c = (char) ('0' + id);
  id = (id - (id % 10)) / 10;
  if (id == 0) {
    shm_name[strlen(BASE_SHM_NAME)] = c;
    shm_name[strlen(BASE_SHM_NAME) + 1] = 0;
    return POOL_THREAD_SUCCESS;
  }

  size_t i;
  for (i = strlen(BASE_SHM_NAME); id != 0; ++i) {
    shm_name[i] = (char) ('0' + (id % 10));
    id = (id - (id % 10)) / 10;
    if (id != 0 && strlen(BASE_SHM_NAME) + 1 == WORD_LEN_MAX) {
      return POOL_THREAD_FAILURE;
    }
  }
  //the null terminating string character
  shm_name[i] = 0;
  return POOL_THREAD_SUCCESS;
}

int pool_thread__shm_name_open(char *name, int *shm_fd) {
  size_t i = 0;
  do {
    name[i] = BASE_SHM_NAME[i];
    ++i;
  } while (BASE_SHM_NAME[i] != 0);
  size_t index = 0;
  do {
    errno = 0;
    int pool_thread__generate_shm_name_r =
        pool_thread__generate_shm_name(name, index);
    if (pool_thread__generate_shm_name_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %d", "pool_thread__generate_shm_name",
          pool_thread__generate_shm_name_r);
      return POOL_THREAD_FAILURE;
    }
    *shm_fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (*shm_fd == -1 && errno != EEXIST) {
      PRINT_ERR("%s : %s", "shm_open", strerror(errno));
      return POOL_THREAD_FAILURE;
    }
    ++index;
  } while (errno == EEXIST);
  errno = 0;

  return POOL_THREAD_SUCCESS;
}
