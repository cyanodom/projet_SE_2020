#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stdbool.h>

#include "macros.h"
#include "pool_thread.h"
#include "shm.h"


#define STR_PREFIX_THREAD " [T] - "
#define STR_THREAD_WORK STR_PREFIX_THREAD "Un thread travaille"
#define STR_THREAD_WAIT STR_PREFIX_THREAD "Un thread attends du travail"
#define STR_THREAD_DONE STR_PREFIX_THREAD "Un thread a fini son travail"
#define STR_CHILD_PROCESS_RETURNED "Un sous processus de thread a renvoyé"
#define STR_THREAD_DEAD STR_PREFIX_THREAD "Un thread s'est détruit"
#define STR_COMMAND_LAUNCHED STR_PREFIX_THREAD "Une commande a été lancée"
#define STR_THREAD_PROBLEM "Un thread néttoyé s'est terminé avec un code "     \
    "d'erreur indiquant un problème"

#define STR_TRHREAD_CORRECT_END "Un thread a eu une belle fin"
#define STR_THREAD_SHOULD_WORK "Une requête a été envoyée à un thread"
#define STR_THREAD_DELETED "Un thread a été libéré"
#define STR_THREAD_CREATED "Un thread a été créé"
#define STR_NO_ENOUGHT_SPACE_SHM "L'espace de mémoire partagée ne possède pas "\
    "suffisement de mémoire"
#define STR_NAME_COMMAND_IS "Le nom de la commande est"
#define STR_UNKNOW_COMMAND "Une commande inconnue a été envoyée"


#define STR_POOL_THREAD_SUCCESS "Tout s'est bien passé"
#define STR_POOL_THREAD_FAILURE "Une erreur s'est produite"
#define STR_POOL_TRHEAD_NO_MORE_THREAD "Il n'y a plus de threads disponible"
#define STR_POOL_THREAD_BAD_ALLOC "Une allocation ou initialisation a mal "    \
    "tourné"
#define STR_POOL_THREAD_SYNC_ERROR "Une erreur de synchronisation s'est "      \
    "produite"
#define STR_POOL_THREAD_BAD_FAILURE "Une grave erreur s'est produite"
#define STR_NO_CORRESPONDING_ERROR_CODE "Le code d'erreur ne correspond pas à "\
    "code d'erreur valable"


#define FD_KILL -1
#define BASE_SHM_NAME "shm_thread_"


struct pool_thread {
  shared_memory **shm_obj;

  pthread_t *threads;
  bool *threads_can_create;
  size_t max_connect_per_thread;
  size_t min_thread_nb;
  size_t max_thread_nb;
  size_t nb_threads;
  size_t shm_size;

  char critical_shm_name[WORD_LEN_MAX];
  int critical_shm_fd;

  sem_t *threads_need_to_work;
  sem_t *threads_work;
  sem_t *threads_need_join;
};

typedef struct thread_arg {
  shared_memory **shm_obj;

  sem_t *thread_need_to_work;
  sem_t *thread_work;
  sem_t *thread_need_join;

  size_t max_connect;
  size_t shm_size;

  int *critical_shm_fd;
  size_t thread_id;
  char *critical_shm_name;
} thread_arg;


static void *pool_thread__run(void *param);
static int pool_thread__shm_name_open(pool_thread *pool, size_t i, char *name,
    int *shm_fd);
static int pool_thread__send_shm(pool_thread *pool, char *name, int fd,
    size_t index_thread);
static int pool_thread__create_thread(pool_thread *pool, size_t i);
static int pool_thread__where_can_create_thread(pool_thread *pool,
    size_t *index_thread);


int pool_thread_init(pool_thread **pool_ret, size_t min_thread_nb,
    size_t max_thread_nb, size_t max_connect_per_thread, size_t shm_size) {

  pool_thread *pool = malloc(sizeof(pool_thread));
  if (pool_ret == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  };
  *pool_ret = pool;

  pool->shm_obj = malloc(sizeof(shared_memory *) * max_thread_nb);
  if (pool->shm_obj == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }

  pool->nb_threads = 0;
  pool->critical_shm_name[0] = 0;
  pool->critical_shm_fd = FD_KILL;
  pool->max_connect_per_thread = max_connect_per_thread;

  pool->min_thread_nb = min_thread_nb;
  pool->max_thread_nb = max_thread_nb;
  pool->shm_size = shm_size;
  pool->threads = malloc(sizeof(pthread_t) * max_thread_nb);
  if (pool->threads == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  pool->threads_can_create = malloc(sizeof(bool) * max_thread_nb);
  if (pool->threads_can_create == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  pool->threads_need_to_work = malloc(sizeof(sem_t) * max_thread_nb);
  if (pool->threads_need_to_work == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  pool->threads_work = malloc(sizeof(sem_t) * max_thread_nb);
  if (pool->threads_work == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  pool->threads_need_join = malloc(sizeof(sem_t) * max_thread_nb);
  if (pool->threads_need_join == NULL) {
    PRINT_ERR("%s : %s", "malloc", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }

  size_t i;
  for (i = 0; i < min_thread_nb; ++i) {
    int pool_thread__create_thread_r = pool_thread__create_thread(pool, i);
    if (pool_thread__create_thread_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %s", "pool_thread__create_thread",
          pool_thread_strerror(pool_thread__create_thread_r));
      return pool_thread__create_thread_r;
    }
  }

  while (i < max_thread_nb) {
    pool->threads_can_create[i] = true;
    ++i;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_dispose(pool_thread **pool) {
  for (size_t i = 0; i < (*pool)->max_thread_nb; ++i) {
    if (!(*pool)->threads_can_create[i]) {
      //say the thread he need to be killed
      int is_dead = 0;
      if (sem_getvalue(&(*pool)->threads_need_join[i], &is_dead) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_SYNC_ERROR;
      }
      if (!is_dead) {
        int pool_thread__send_shm_r = pool_thread__send_shm(*pool, "\0",
            FD_KILL, i);
        if (pool_thread__send_shm_r != POOL_THREAD_SUCCESS) {
            PRINT_ERR("%s : %s", "pool_thread__send_shm",
                pool_thread_strerror(pool_thread__send_shm_r));
            return pool_thread__send_shm_r;
        }
      }
      //no need to wait for thread_need_join beacause join does this
      void *ret;
      if (pthread_join((*pool)->threads[i], &ret) != 0) {
        PRINT_ERR("%s : %s", "pthread_join", strerror(errno));
        return POOL_THREAD_BAD_FAILURE;
      }

      if (ret == PTHREAD_CANCELED) {
        PRINT_WARN("%s : %s", STR_THREAD_PROBLEM, strerror(errno));
      } else {
        PRINT_INFO("%s", STR_TRHREAD_CORRECT_END)
      }

      if (sem_destroy(&(*pool)->threads_work[i]) != 0) {
        PRINT_ERR("%s : %s", "sem_destroy", strerror(errno));
        return POOL_THREAD_BAD_FAILURE;
      }
      if (sem_destroy(&(*pool)->threads_need_to_work[i]) != 0) {
        PRINT_ERR("%s : %s", "sem_destroy", strerror(errno));
        return POOL_THREAD_BAD_FAILURE;
      }
      if (sem_destroy(&(*pool)->threads_need_join[i]) != 0) {
        PRINT_ERR("%s : %s", "sem_destroy", strerror(errno));
        return POOL_THREAD_BAD_FAILURE;
      }
      PRINT_INFO("%s", STR_THREAD_DELETED);
    }
  }
  free((*pool)->shm_obj);

  free((*pool)->threads);
  free((*pool)->threads_work);
  free((*pool)->threads_need_to_work);
  free((*pool)->threads_need_join);
  free((*pool)->threads_can_create);
  free(*pool);
  *pool = NULL;
  return POOL_THREAD_SUCCESS;
}

//enrôle un thread pour la shm shm_name
int pool_thread_enroll(pool_thread *pool, char *shm_name) {
  //find a free thread
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (!pool->threads_can_create[i]) {
      int is_dead = 0;
      if (sem_getvalue(&pool->threads_need_join[i], &is_dead) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_BAD_FAILURE;
      }
      if (!is_dead) {
        int work = 0;
        if (sem_getvalue(&pool->threads_work[i], &work) != 0) {
          PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
          return POOL_THREAD_BAD_FAILURE;
        }
        if (!work) {
          // this thread is free
          int shm_fd;
          int pool_thread__shm_name_open_r = pool_thread__shm_name_open(
              pool, i, shm_name, &shm_fd);
          if (pool_thread__shm_name_open_r != POOL_THREAD_SUCCESS) {
            PRINT_ERR("%s : %s", "pool_thread__shm_name_open",
                pool_thread_strerror(pool_thread__shm_name_open_r));
            return pool_thread__shm_name_open_r;
          }
          int pool_thread__send_shm_r = pool_thread__send_shm(pool, shm_name,
              shm_fd, i);
          if (pool_thread__send_shm_r != POOL_THREAD_SUCCESS) {
            PRINT_ERR("%s : %s", "pool_thread__send_shm",
                pool_thread_strerror(pool_thread__send_shm_r));
            return POOL_THREAD_FAILURE;
          }
          return POOL_THREAD_SUCCESS;
        }
      }
    }
  }
  //there's no available threads, find a place to create a new one
  size_t index_thread;
  int pool_thread__where_can_create_thread_r =
      pool_thread__where_can_create_thread(pool, &index_thread);
  if (pool_thread__where_can_create_thread_r != POOL_THREAD_SUCCESS) {
    if (pool_thread__where_can_create_thread_r == POOL_TRHEAD_NO_MORE_THREAD) {
      PRINT_WARN("%s : %s", "pool_thread__create_thread",
          pool_thread_strerror(pool_thread__where_can_create_thread_r));
    } else {
      PRINT_ERR("%s : %s", "pool_thread__create_thread",
          pool_thread_strerror(pool_thread__where_can_create_thread_r));
    }
    return pool_thread__where_can_create_thread_r;
  }
  //create it
  int pool_thread__create_thread_r =
      pool_thread__create_thread(pool, index_thread);
  if (pool_thread__create_thread_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %s", "pool_thread__create_thread",
        pool_thread_strerror(pool_thread__create_thread_r));
    return pool_thread__create_thread_r;
  }
  //and create and send shm
  int shm_fd;
  int pool_thread__shm_name_open_r = pool_thread__shm_name_open(pool,
      index_thread, shm_name, &shm_fd);
  if (pool_thread__shm_name_open_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %s", "pool_thread__shm_name_open",
        pool_thread_strerror(pool_thread__shm_name_open_r));
    return pool_thread__shm_name_open_r;
  }
  int pool_thread__send_shm_r = pool_thread__send_shm(pool, shm_name,
      shm_fd, index_thread);
  if (pool_thread__send_shm_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %s", "pool_thread__send_shm",
        pool_thread_strerror(pool_thread__send_shm_r));
    return POOL_THREAD_FAILURE;
  }
  return POOL_THREAD_SUCCESS;
}

int pool_thread_manage(pool_thread *pool) {
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (!pool->threads_can_create[i]) {
      int is_dead = 0;
      if (sem_getvalue(&pool->threads_need_join[i], &is_dead) != 0) {
        PRINT_ERR("%s : %s", "sem_getvalue", strerror(errno));
        return POOL_THREAD_BAD_FAILURE;
      }
      if (is_dead) {
        if (sem_wait(&pool->threads_need_join[i]) != 0) {
          PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
          return POOL_THREAD_SYNC_ERROR;
        }

        void *ret;
        if (pthread_join(pool->threads[i], &ret) != 0) {
          PRINT_ERR("%s : %s", "pthread_join", strerror(errno));
          return POOL_THREAD_BAD_FAILURE;
        }
        if (ret == PTHREAD_CANCELED) {
          PRINT_WARN("%s : %s", STR_THREAD_PROBLEM, strerror(errno));
        } else {
          PRINT_INFO("%s", STR_TRHREAD_CORRECT_END)
        }

        PRINT_INFO("%s", STR_THREAD_DELETED);
        pool->threads_can_create[i] = true;
        --pool->nb_threads;
      }
    }
  }

  while (pool->nb_threads < pool->min_thread_nb) {
    size_t index_thread;
    int pool_thread__where_can_create_thread_r =
        pool_thread__where_can_create_thread(pool, &index_thread);
    if (pool_thread__where_can_create_thread_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %s", "pool_thread__where_can_create_thread",
          pool_thread_strerror(pool_thread__where_can_create_thread_r));
      return pool_thread__where_can_create_thread_r;
    }
    int pool_thread__create_thread_r =
        pool_thread__create_thread(pool, index_thread);
    if (pool_thread__create_thread_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %s", "pool_thread__create_thread",
          pool_thread_strerror(pool_thread__create_thread_r));
      return POOL_THREAD_FAILURE;
    }
  }

  return POOL_THREAD_SUCCESS;
}

char *pool_thread_strerror(int errnum) {
  switch (errnum) {
    case POOL_THREAD_SUCCESS:
      return STR_POOL_THREAD_SUCCESS;
    case POOL_THREAD_FAILURE:
      return STR_POOL_THREAD_FAILURE;
    case POOL_TRHEAD_NO_MORE_THREAD:
      return STR_POOL_TRHEAD_NO_MORE_THREAD;
    case POOL_THREAD_BAD_ALLOC:
      return STR_POOL_THREAD_BAD_ALLOC;
    case POOL_THREAD_SYNC_ERROR:
      return STR_POOL_THREAD_SYNC_ERROR;
    case POOL_THREAD_BAD_FAILURE:
      return STR_POOL_THREAD_BAD_FAILURE;
    default :
      return STR_NO_CORRESPONDING_ERROR_CODE;
  }
}
//__________________________THREAD_CODE____________________________
//fonction des threads
void *pool_thread__run(void *param) {
  void *ret = NULL;
  thread_arg *arg = (thread_arg *) param;
  int shm_fd;
  size_t remaining_work_plus_one = arg->max_connect
    + (arg->max_connect == 0 ? 1 : 0);
  bool continue_same_client = false;
  do {
  //wait to need to work
    if (!continue_same_client) {
      PRINT_INFO("%s", STR_THREAD_WAIT);
      if (sem_wait(arg->thread_need_to_work) == -1) {
        PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
        ret = PTHREAD_CANCELED;
        goto thread_die;
      }
      // now I work, get the shm
      shm_fd = *arg->critical_shm_fd;
      //say to others I'm working
      if (sem_post(arg->thread_work) == -1) {
        PRINT_ERR("%s : %s", "sem_post", strerror(errno));
        ret = PTHREAD_CANCELED;
        goto thread_die;
      }
    }
    if (shm_fd != FD_KILL) {

      PRINT_INFO("%s", STR_THREAD_WORK);

      int pipe_fd[2];
      if (pipe(pipe_fd) != 0) {
        PRINT_ERR("%s : %s", "pipe", strerror(errno));
        ret = PTHREAD_CANCELED;
        goto thread_die;
      }
      PRINT_MSG("%s", STR_COMMAND_LAUNCHED);

      //___________work___________
      int r = fork();
      if (r == -1) {
        PRINT_ERR("%s : %s", "fork", strerror(errno));
        ret = PTHREAD_CANCELED;
        goto thread_die;
      } else if (r == 0) {
        //fils

        //wait for the client to send exec name
        if (sem_wait(&(*arg->shm_obj)->client_send) != 0) {
          PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }

        char word[WORD_LEN_MAX];
        size_t index_d = 0;
        size_t index_arg = 0;
        do {
          if ((*arg->shm_obj)->data[index_d] != ' ') {
            if ((*arg->shm_obj)->data[index_d + 1] == 0
                || (*arg->shm_obj)->data[index_d + 1] == ' ') {
              ++index_arg;
            }
          }
          ++index_d;
        } while ((*arg->shm_obj)->data[index_d] != 0);

        char **args = malloc(sizeof(char *) * (index_arg + 2));
        index_arg = 0;
        index_d = 0;
        size_t index_w = 0;

        do {
          if ((*arg->shm_obj)->data[index_d] != ' ') {
            word[index_w] = (*arg->shm_obj)->data[index_d];
            ++index_w;
            if ((*arg->shm_obj)->data[index_d + 1] == 0
                || (*arg->shm_obj)->data[index_d + 1] == ' ') {
              word[index_w] = 0;
              args[index_arg] = malloc(sizeof(char) * (strlen(word) + 1));
              if (args[index_arg] == NULL) {
               PRINT_ERR("%s : %s", "malloc", strerror(errno));
               ret = PTHREAD_CANCELED;
               goto thread_die;
              }
              strcpy(args[index_arg], word);
              index_w = 0;
              ++index_arg;
              args[index_arg] = NULL;
            }
          }
          ++index_d;
        } while ((*arg->shm_obj)->data[index_d] != 0);

        PRINT_INFO("%s : %s", STR_NAME_COMMAND_IS, (*arg->shm_obj)->data);

        if (close(pipe_fd[0]) != 0) {
          PRINT_ERR("%s : %s", "close", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        if (close(STDOUT_FILENO) != 0) {
          PRINT_ERR("%s : %s", "close", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
          PRINT_ERR("%s : %s", "dup2", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        if (close(pipe_fd[1]) != 0) {
          PRINT_ERR("%s : %s", "close", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }

        execvp(args[0], args);
        ret = PTHREAD_CANCELED;
        goto thread_die;
      } else {
        //père
        if (close(pipe_fd[1]) != 0) {
          PRINT_ERR("%s : %s", "close", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        //read output of exec process
        size_t i = 0;
        char c;
        while (read(pipe_fd[0], &c, sizeof(char)) > 0) {
          if (!(i < arg->shm_size - 1)) {
            PRINT_ERR("%s", STR_NO_ENOUGHT_SPACE_SHM);
            ret = PTHREAD_CANCELED;
            goto thread_die;
          }
          (*arg->shm_obj)->data[i] = c;
          ++i;
        }
        (*arg->shm_obj)->data[i] = 0;

        //and send client the output is available
        if (sem_post(&(*arg->shm_obj)->thread_send) != 0) {
          PRINT_ERR("%s : %s", "sem_post", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        if (close(pipe_fd[0]) != 0) {
          PRINT_ERR("%s : %s", "close", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        //wait the program
        int status;
        if (waitpid(r, &status, 0) == -1) {
          PRINT_ERR("%s : %s", "wait", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
        if (WIFEXITED(status)) {
          PRINT_MSG("%s : %d", STR_CHILD_PROCESS_RETURNED,
              WEXITSTATUS(status));
        }
      }

      if (sem_wait(&(*arg->shm_obj)->client_send) != 0) {
        PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
        ret = PTHREAD_CANCELED;
        goto thread_die;
      }

      if (strcmp((*arg->shm_obj)->data, END) == 0) {
        continue_same_client = false;
      } else if (strcmp((*arg->shm_obj)->data, NEW) == 0) {
        continue_same_client = true;
        if (sem_post(&(*arg->shm_obj)->new_command_ready) != 0) {
          PRINT_ERR("%s : %s", "sem_post", strerror(errno));
          ret = PTHREAD_CANCELED;
          goto thread_die;
        }
      } else {
        PRINT_ERR("%s", STR_UNKNOW_COMMAND);
        ret = PTHREAD_CANCELED;
        goto thread_die;
      }
    }
    if (!continue_same_client) {
      PRINT_INFO("%s", STR_THREAD_DONE);
      //finish work and saying others I'm free
      if (sem_wait(arg->thread_work) == -1) {
        PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
        ret = PTHREAD_CANCELED;
        goto thread_die;
      }
    }
    if (remaining_work_plus_one != 0) {
           --remaining_work_plus_one;
    }
  } while (shm_fd != FD_KILL && remaining_work_plus_one != 1);
thread_die:
  if (continue_same_client) {
    if (sem_wait(arg->thread_work) == -1) {
      PRINT_ERR("%s : %s", "sem_wait", strerror(errno));
      ret = PTHREAD_CANCELED;
      goto thread_die;
    }
  }
  //thread need to die
  //say to others I need join
  if (sem_post(arg->thread_need_join) == -1) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    pthread_exit(PTHREAD_CANCELED);
  }
  free(arg);
  PRINT_INFO("%s", STR_THREAD_DEAD);
  pthread_exit(ret);
}

//__________________________POOL_THREAD_TOOLS______________________
static int pool_thread__generate_shm_name(char *shm_name, size_t id);


//envois la shm
int pool_thread__send_shm(pool_thread *pool, char *name, int fd,
    size_t index_thread) {
  pool->critical_shm_fd = fd;
  //copy the shm_name into global_shm_name
  size_t i = 0;
  if (name[0] != 0) {
    do {
      pool->critical_shm_name[i] = name[i];
      ++i;
    } while (name[i] != 0);
  }
  pool->critical_shm_name[i] = 0;
  //dire au thread de travailler
  if (sem_post(&pool->threads_need_to_work[index_thread]) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_SYNC_ERROR;
  }
  PRINT_INFO("%s", STR_THREAD_SHOULD_WORK);
  //attendre qu'il travaille et donc qu'il ai récupéré la shm
  if (sem_wait(&pool->threads_work[index_thread]) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_SYNC_ERROR;
  }
  //restaurer threads_work
  if (sem_post(&pool->threads_work[index_thread]) != 0) {
    PRINT_ERR("%s : %s", "sem_post", strerror(errno));
    return POOL_THREAD_SYNC_ERROR;
  }
  return POOL_THREAD_SUCCESS;
}

//crée le thread i avec ses sémaphores
int pool_thread__create_thread(pool_thread *pool, size_t i) {
  thread_arg *arg = malloc(sizeof(thread_arg));
  arg->max_connect = pool->max_connect_per_thread;
  arg->shm_size = pool->shm_size;
  arg->thread_id = i;

  arg->thread_need_to_work = &pool->threads_need_to_work[i];
  arg->thread_work = &pool->threads_work[i];
  arg->thread_need_join = &pool->threads_need_join[i];

  arg->critical_shm_name = pool->critical_shm_name;
  arg->critical_shm_fd = &pool->critical_shm_fd;

  if (sem_init(arg->thread_work, 0, 0) != 0) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  if (sem_init(arg->thread_need_to_work, 0, 0) != 0) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  if (sem_init(arg->thread_need_join, 0, 0) != 0) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }

  pool->threads_can_create[i] = false;

  arg->shm_obj = &pool->shm_obj[i];

  int err = pthread_create(&pool->threads[i], NULL, pool_thread__run, arg);
  if (err != 0) {
    PRINT_ERR("%s : %s", "pthread_create", strerror(err));
    free(&pool->threads[i]);
    if (sem_post(&pool->threads_need_join[i]) != -1) {
      PRINT_ERR("%s : %s", "sem_init", strerror(errno));
      return POOL_THREAD_SYNC_ERROR;
    }
    return POOL_THREAD_FAILURE;
  }
  PRINT_INFO("%s", STR_THREAD_CREATED);
  ++pool->nb_threads;

  return POOL_THREAD_SUCCESS;
}

//trouve un endroit pour créer un thread et le renvois dans index_thread si
//il existe
int pool_thread__where_can_create_thread(pool_thread *pool,
    size_t *index_thread) {
  for (size_t i = 0; i < pool->max_thread_nb; ++i) {
    if (pool->threads_can_create[i]) {
      *index_thread = i;
      return POOL_THREAD_SUCCESS;
    }
  }
  return POOL_TRHEAD_NO_MORE_THREAD;
}

int pool_thread__shm_name_open(pool_thread *pool, size_t id, char *name,
    int *shm_fd) {
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
      PRINT_ERR("%s : %s", "pool_thread__generate_shm_name",
          pool_thread_strerror(pool_thread__generate_shm_name_r));
      return POOL_THREAD_FAILURE;
    }
    *shm_fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR
        | S_IWUSR);
    if (*shm_fd == -1 && errno != EEXIST) {
      PRINT_ERR("%s : %s", "shm_open", strerror(errno));
      return POOL_THREAD_FAILURE;
    }
    ++index;
  } while (errno == EEXIST);
  errno = 0;

  size_t real_shm_size = (pool->shm_size + SHM_HEADER);

  if (ftruncate(*shm_fd, (__off_t) real_shm_size) != 0) {
    PRINT_ERR("%s : %s", "ftruncate", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  char *shm_ptr = mmap(NULL, real_shm_size, PROT_READ | PROT_WRITE,
      MAP_SHARED, *shm_fd, 0);
  if (shm_ptr == MAP_FAILED) {
    PRINT_ERR("%s : %s", "mmap", strerror(errno));
    return POOL_THREAD_FAILURE;
  }

  pool->shm_obj[id] = (shared_memory *)shm_ptr;

  if (sem_init(&pool->shm_obj[id]->thread_send, 1, 0) != 0) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  if (sem_init(&pool->shm_obj[id]->new_command_ready, 1, 1) != 0) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  if (sem_init(&pool->shm_obj[id]->client_send, 1, 0) != 0) {
    PRINT_ERR("%s : %s", "sem_init", strerror(errno));
    return POOL_THREAD_BAD_ALLOC;
  }
  return POOL_THREAD_SUCCESS;
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
