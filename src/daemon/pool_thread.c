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

#define STR_THREAD_WAIT_TO_WORK "Un thread attends de travailler"
#define STR_THREAD_HAS_FINISHED_WORK "Un thread a fini son travail"
#define STR_CREATE_A_THREAD "Un nouveau thread a été créé"
#define STR_A_THREAD_IS_DEAD "Un thread s'est terminé"
#define STR_A_THREAD_WORK "Un thread à commencé son travail"

#define FD_KILL -1
#define BASE_SHM_NAME "shm_thread_"

static size_t max_connect;

//Everytime global_nb_threads is very near the real number of threads
static size_t global_nb_threads;

static int global_shm_fd;
static char global_shm_name[WORD_LEN_MAX];

struct pool_thread {
  pthread_t **threads;
  sem_t *sem_thread_get_shm;
  sem_t *sem_isnt_working;
  sem_t *sem_thread_stopped;
  sem_t *sem_thread_work;
  sem_t *sem_mutex_shm;
  sem_t *sem_daemon_send_shm;
  sem_t *sem_mutex_nb;
  size_t max_thread_nb;
  size_t min_thread_nb;
  size_t shm_size;
};

typedef struct thread_arg {
  sem_t *sem_mutex_shm;
  sem_t *sem_mutex_nb;
  sem_t *sem_thread_get_shm;
  sem_t *sem_daemon_send_shm;
  sem_t *sem_isnt_working;
  sem_t *sem_thread_work;
  sem_t *sem_thread_stopped;
} thread_arg;

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
  (*pool)->sem_thread_stopped = malloc(sizeof(sem_t) * max_thread_nb);
  if ((*pool)->sem_thread_stopped == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  (*pool)->sem_mutex_shm = malloc(sizeof(sem_t));
  if ((*pool)->sem_mutex_shm == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  (*pool)->sem_mutex_nb = malloc(sizeof(sem_t));
  if ((*pool)->sem_mutex_nb == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  (*pool)->sem_thread_get_shm = malloc(sizeof(sem_t));
  if ((*pool)->sem_thread_get_shm == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  (*pool)->sem_daemon_send_shm = malloc(sizeof(sem_t));
  if ((*pool)->sem_daemon_send_shm == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  (*pool)->sem_isnt_working = malloc(sizeof(sem_t) * max_thread_nb);
  if ((*pool)->sem_isnt_working == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init((*pool)->sem_mutex_nb, 0, 1) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init((*pool)->sem_thread_get_shm, 0, 1) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init((*pool)->sem_daemon_send_shm, 0, 0) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init((*pool)->sem_mutex_shm, 0, 1) == -1) {
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
      sem_destroy(&(*pool)->sem_thread_work[i]);
      sem_destroy(&(*pool)->sem_isnt_working[i]);
      sem_destroy(&(*pool)->sem_thread_stopped[i]);
      free((*pool)->threads[i]);
    }
  }
  free((*pool)->sem_mutex_nb);
  free((*pool)->sem_thread_get_shm);
  free((*pool)->sem_daemon_send_shm);
  free((*pool)->sem_mutex_shm);
  free((*pool)->sem_thread_work);
  free((*pool)->sem_isnt_working);
  free((*pool)->sem_thread_stopped);
  free((*pool)->threads);

  sem_destroy((*pool)->sem_mutex_nb);
  sem_destroy((*pool)->sem_thread_get_shm);
  sem_destroy((*pool)->sem_daemon_send_shm);
  sem_destroy((*pool)->sem_mutex_shm);

  free(*pool);
  *pool = NULL;
  return POOL_THREAD_SUCCESS;
}

int pool_thread_enroll(pool_thread *pool, char *shm_name) {
  //find a free thread
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (pool->threads[i] != NULL) {
      int sem_value = 0;
      if (sem_getvalue(&pool->sem_isnt_working[i], &sem_value) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_FAILURE;
      }
      if (sem_value == 1) {
        // this thread is free
        int shm_fd;
        int pool_thread__shm_name_open_r = pool_thread__shm_name_open(shm_name,
            &shm_fd);
        if (pool_thread__shm_name_open_r != POOL_THREAD_SUCCESS) {
          PRINT_ERR("%s : %d", "pool_thread__shm_name_open",
              pool_thread__shm_name_open_r);
              return POOL_THREAD_FAILURE;
        }
        int pool_thread__send_shm_r = pool_thread__send_shm(pool, shm_name,
            shm_fd, i);
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
  if (sem_wait(pool->sem_mutex_nb) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  size_t nb_threads = global_nb_threads;
  if (sem_post(pool->sem_mutex_nb) == -1) {
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
  //initialize
  thread_arg *arg = (thread_arg *) param;

  char shm_name[WORD_LEN_MAX];
  int shm_fd;
  size_t remaining_work_plus_one = max_connect + (max_connect != 0 ? 1 : 0);
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
    //alert other that I'm ready
    if (sem_post(arg->sem_isnt_working) == -1) {
      PRINT_ERR("%s : %s", "sem_post", strerror(errno));
      exit(EXIT_FAILURE);
    }
    //wait to work
    PRINT_INFO("%s", STR_THREAD_WAIT_TO_WORK);
    if (sem_wait(arg->sem_thread_work) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (sem_wait(arg->sem_isnt_working) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      exit(EXIT_FAILURE);
    }

    // now I work, wait to get shm fd
    printf("ready\n");
    if (sem_wait(arg->sem_daemon_send_shm) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      exit(EXIT_FAILURE);
    }
    printf("get\n");
    size_t index_str = 0;
    if (global_shm_name[0] != 0) {
      do {
        shm_name[index_str] = global_shm_name[index_str];
        ++index_str;
      } while (global_shm_name[index_str] != 0);
    }
    shm_name[index_str] = 0;
    shm_fd = global_shm_fd;
    if (sem_post(arg->sem_thread_get_shm) == -1) {
      PRINT_ERR("%s : %s", "sem_post", strerror(errno));
      exit(EXIT_FAILURE);
    }
    if (shm_fd != FD_KILL) {
      PRINT_MSG("%s", STR_A_THREAD_WORK);
      //TODO fork

      sleep(5);

      //finish work by closing shm and unlock sem
      PRINT_MSG("%s", STR_THREAD_HAS_FINISHED_WORK);
      if (shm_unlink(shm_name) != 0) {
        PRINT_ERR("%s : %s", "shm_unlink", strerror(errno));
        exit(EXIT_FAILURE);
      }
      if (close(shm_fd) != 0) {
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
  //if the thread need to be disposed
  if (sem_post(arg->sem_isnt_working) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    exit(EXIT_FAILURE);
  }
  free(arg);
  PRINT_INFO("%s", STR_A_THREAD_IS_DEAD);
  pthread_exit(NULL);
}

int pool_thread__send_shm(pool_thread *pool, char *shm_name, int fd,
    size_t index_thread) {
  if (sem_wait(&pool->sem_isnt_working[index_thread]) == -1) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (sem_post(&pool->sem_isnt_working[index_thread]) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (sem_wait(pool->sem_thread_get_shm) != 0) {
    PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_wait(pool->sem_mutex_shm) != 0) {
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
  if (sem_post(pool->sem_mutex_shm) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_post(&pool->sem_thread_work[index_thread]) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_post(pool->sem_daemon_send_shm) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread__create_thread(pool_thread *pool, size_t i) {
  PRINT_INFO("%s", STR_CREATE_A_THREAD);
  if (sem_init(&pool->sem_thread_work[i], 0, 0) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }
  if (sem_init(&pool->sem_isnt_working[i], 0, 0) == -1) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  pool->threads[i] = malloc(sizeof(pthread_t));
  if (pool->threads[i] == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  thread_arg *arg = malloc(sizeof(thread_arg));

  arg->sem_mutex_shm = pool->sem_mutex_shm;
  arg->sem_thread_stopped = pool->sem_thread_stopped;
  arg->sem_mutex_nb = pool->sem_mutex_nb;
  arg->sem_daemon_send_shm = pool->sem_daemon_send_shm;
  arg->sem_daemon_send_shm = pool->sem_thread_get_shm;
  arg->sem_isnt_working = &pool->sem_isnt_working[i];
  arg->sem_thread_work = &pool->sem_thread_work[i];
  int err = pthread_create(pool->threads[i], NULL, pool_thread__run, arg);

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
