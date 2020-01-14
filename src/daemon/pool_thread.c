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

//Everytime global_nb_threads is very near the real number of threads

struct pool_thread {
  pthread_t *threads;
  size_t max_connect_per_thread;
  size_t min_thread_nb;
  size_t max_thread_nb;
  size_t nb_threads;

  char critical_shm_name[WORD_LEN_MAX];
  int critical_shm_fd;

  sem_t shm_get;
  sem_t shm_sent

  sem_t *threads_need_to_work;
  sem_t *threads_work;
  sem_t *threads_dead;
};

typedef struct thread_arg {
  sem_t *shm_get;
  sem_t *shm_sent;

  sem_t *thread_need_to_work;
  sem_t *thread_work;
  sem_t *thread_dead;
  int *critical_shm_fd;
  char *critical_shm_name;
} thread_arg;

static void *pool_thread__run(void *param);
static int pool_thread__shm_name_open(pool_thread *pool, char *name,
    int *shm_fd);
static int pool_thread__send_shm(pool_thread *pool, char *shm_name, int fd,
    size_t index_thread);
static int pool_thread__create_thread(pool_thread *pool, size_t i);
static int pool_thread__where_can_create_thread(pool_thread *pool,
    size_t *index_thread);

//remplis le tableau threads de taille supposée max_thread_nb avec les id des
//threads ou NULL si aucun thread n'est associé
int pool_thread_init(pool_thread **pool_ret, size_t min_thread_nb,
    size_t max_thread_nb, size_t max_connect_per_thread, size_t shm_size) {
  //create pool and threads
  *pool_ret = malloc(sizeof(pool_thread));
  if (*pool == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  pool_thread *pool = *pool_ret;
  pool->nb_threads = 0;
  pool->critical_shm_name = "\0";
  pool->critical_shm_fd = FD_KILL;
  pool->max_connect_per_thread = max_connect_per_thread;

  pool->min_thread_nb = min_thread_nb;
  pool->max_thread_nb = max_thread_nb;
  pool->shm_size = shm_size;
  pool->threads = malloc(sizeof(pthread_t) * max_thread_nb);
  if (pool->threads == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  pool->threads_need_to_work = malloc(sizeof(sem_t) * max_thread_nb);
  if (pool->threads_need_to_work == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  pool->threads_work = malloc(sizeof(sem_t) * max_thread_nb);
  if (pool->threads_work == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  pool->threads_dead = malloc(sizeof(sem_t) * max_thread_nb);
  if (pool->threads_dead == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  // create shm
  if (sem_init(pool->shm_get, 0, 0) != -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(pool->shm_send, 0, 0) != -1) {
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

  //set the ohter threads to dead
  while (i < max_thread_nb) {
    if (sem_init(&pool->threads_dead[i], 0, 1) == -1) {
      PRINT_ERR("%s : %s", "sem_init", strerror(errno));
      return POOL_THREAD_FAILURE;
    }
    ++i;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_dispose(pool_thread **pool) {
  return POOL_THREAD_SUCCESS;
}

int pool_thread_enroll(pool_thread *pool, char *shm_name) {
  //find a free thread
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    int is_dead = 0;
    if (sem_getvalue(&pool->threads_dead[i], &sem_value) != 0) {
      PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
      return POOL_THREAD_FAILURE;
    }
    if (!is_dead) {
      int work = 0;
      if (sem_getvalue(&pool->threads_work[i], &work) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_FAILURE;
      }
      if (!work) {
        // this thread is free
        int shm_fd;
        int pool_thread__shm_name_open_r = pool_thread__shm_name_open(
            pool, shm_name, &shm_fd);
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
      int sem_value = 0;
      if (sem_getvalue(&pool->sem_thread_work[i], &sem_value) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_FAILURE;
      }
      if (sem_value == THREAD_DEAD) {
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
  thread_arg *arg = (thread_arg *) param;
  char shm_name[WORD_LEN_MAX];
  int shm_fd;
  size_t remaining_work_plus_one = max_connect + (max_connect == 0 ? 1 : 0);
  //wait to increment global_nb_threads

  if (sem_wait(arg->sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    exit(EXIT_FAILURE);
  }
  ++global_nb_threads;
  if (sem_post(arg->sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    exit(EXIT_FAILURE);
  }
  do {
    //wait to work
    for (size_t i = 0; i < THREAD_WORK; ++i) {
      PRINT_INFO("%s", STR_THREAD_WAIT);
      if (sem_wait(arg->sem_thread_work) == -1) {
        PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
        exit(EXIT_FAILURE);
      }
    }
    // now I work, wait to get shm fd
    if (sem_wait(arg->sem_daemon_send) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      exit(EXIT_FAILURE);
    }
    size_t index_str = 0;
    if (global_shm_name[0] != 0) {
      do {
        shm_name[index_str] = arg->shm_name[index_str];
        ++index_str;
      } while (global_shm_name[index_str] != 0);
    }
    shm_name[index_str] = 0;
    shm_fd = global_shm_fd;
    if (sem_post(arg->sem_thread_get) == -1) {
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
      if (close(*arg->shm_fd) != 0) {
        PRINT_ERR("%s : %s", "close", strerror(errno));
        exit(EXIT_FAILURE);
      }
    }
    if (remaining_work_plus_one != 0) {
           --remaining_work_plus_one;
    }
  } while (shm_fd != FD_KILL && remaining_work_plus_one != 1);

  //thread need to die
  //wait to decrement global_nb_threads
  if (sem_wait(arg->sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    //send to me a signal to dispose
    //TODO
    exit(EXIT_FAILURE);
  }
  --global_nb_threads;
  if (sem_post(arg->sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    //send to me a signal to dispose
    //TODO
    exit(EXIT_FAILURE);
  }
  free(arg);
  for (size_t i = 0; i < THREAD_DEAD; ++i) {
    if (sem_post(arg->sem_thread_work) == -1) {
      PRINT_ERR("%s : %s", "sem_post", strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
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
  for (size_t i = 0; i < THREAD_WORK; ++i) {
    if (sem_post(&pool->sem_thread_work[index_thread]) != 0) {
      PRINT_ERR("%s : %s", "sem_post", strerror(errno));
      return POOL_THREAD_FAILURE;
    }
  }
  if (sem_post(&sem_daemon_send) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  return POOL_THREAD_SUCCESS;
}

//crée le thread i avec ses sémaphores
int pool_thread__create_thread(pool_thread *pool, size_t i) {
  thread_arg *arg = malloc(sizeof(thread_arg));
  arg->shm_get = &pool->shm_get;
  arg->shm_send = &pool->shm_send;

  arg->thread_need_to_work = &pool->threads_need_to_work[i];
  arg->thread_work = &pool->threads_work[i];
  arg->thread_dead = &pool->threads_dead[i];

  arg->critical_shm_name = &pool->critical_shm_name;
  arg->critical_shm_fd = &pool->critical_shm_fd;

  if (sem_init(pool->thread_work, 0, 0) != -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(pool->thread_need_to_work, 0, 0) != -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(pool->thread_dead, 0, 0) != -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  int err = pthread_create(pool->threads[i], NULL, pool_thread__run,
        arg);
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
