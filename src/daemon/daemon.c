#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>

#include "load_conf.h"
#include "pool_thread.h"
#include "pipe.h"

#include "macros.h"

#define STR_WAITING_FOR_A_CONNECTION "En attente d'une connection"
#define STR_CLIENT_CONNECTED "Client connecté"
#define STR_WAITING_FOR_THE_CLIENT_PIPE "En attente du nom du pipe client"
#define STR_CREATE_POOL_THREAD "Création du pool de threads"
#define STR_LOAD_CONF "Chargement du fichier de configuration"
#define STR_CREATE_DAEMON_PIPE "Création du pipe du démon"
#define STR_OPEN_DAEMON_PIPE "Ouverture du pipe du démon"
#define STR_READ_PIPE_INFORMATIONS "Lire les informations du pipe"
#define STR_INITIALIZE "Initialiser les variables"
#define STR_PIPE_GET_NAME "Le nom du fichier recu est"
#define STR_DISPOSE "Libération des ressources allouées"
#define STR_NEW_CONNECTION "Connection avec un nouveau client"

static int should_be_killed = false;

static void monitor_signal(int signum);

int main(void) {
  //initialize
  PRINT_INFO("%s", STR_INITIALIZE);
  int r; //error number

  size_t max_thread_nb = 0;
  size_t min_thread_nb = 0;
  size_t max_connect_per_thread = 0;
  size_t shm_size = 0;
  //  according to POSIX -1 is an impossible value and could be used in close
  //    which will return -1 itself but whithout causing errors
  int daemon_pipe_fd = -1;
  int client_pipe_fd = -1;
  char client_pipe[WORD_LEN_MAX];
  pool_thread *pool;
  char shm_name[WORD_LEN_MAX];

  //set the default actions on SIGINT signal
  struct sigaction action;
  action.sa_handler = monitor_signal;
  action.sa_flags = 0;
  if (sigfillset(&action.sa_mask) == -1) {
    perror("sigfillset");
    exit(EXIT_FAILURE);
  }

  if (sigaction(SIGINT, &action, NULL) == -1) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }

  //load the configuration file
  PRINT_INFO("%s", STR_LOAD_CONF);
  int load_conf_ret = load_conf_file(&max_thread_nb, &min_thread_nb,
      &max_connect_per_thread, &shm_size);
  if (load_conf_ret != LOAD_CONF_SUCCESS) {
    PRINT_ERR("%s : %d", "load_conf_file", load_conf_ret);
    goto error;
  }
  PRINT_INFO(STR_MAX_THREAD " = %zu", max_thread_nb);
  PRINT_INFO(STR_MIN_THREAD " = %zu", min_thread_nb);
  PRINT_INFO(STR_MAX_CONNECT_PER_THREAD " = %zu", max_connect_per_thread)
  PRINT_INFO(STR_SHM_SIZE " = %zu", shm_size);

  //create the thread pool and shm
  PRINT_INFO("%s", STR_CREATE_POOL_THREAD);
  int pool_thread_init_r = pool_thread_init(&pool, min_thread_nb,
      max_thread_nb, max_connect_per_thread, shm_size);
  if (pool_thread_init_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %d", "pool_thread_init", pool_thread_init_r);
    goto error;
  }

  //create the daemon pipe
  PRINT_INFO("%s", STR_CREATE_DAEMON_PIPE);
  int pipe_create_base_r = pipe_create_base();
  if (pipe_create_base_r != PIPE_SUCCESS) {
    PRINT_ERR("%s : %d", "pipe_create_base", pipe_create_base_r);
    goto error;
  }

  while (1) {
    if (should_be_killed) {
      goto dispose;
    }
    //open the daemon pipe
    PRINT_INFO("%s", STR_OPEN_DAEMON_PIPE);
    PRINT_MSG("%s", STR_WAITING_FOR_A_CONNECTION);
    daemon_pipe_fd = open(BASE_PIPE_NAME, O_RDONLY);
    if (daemon_pipe_fd == -1) {
      PRINT_ERR("%s : %s", "open", strerror(errno));
      goto error;
    }
    //set the file to be closed on exec :
    if (fcntl(daemon_pipe_fd, F_SETFD, FD_CLOEXEC) == -1) {
      PRINT_ERR("%s : %s", "fcntl", strerror(errno));
      goto error;
    }

    //manage threads
    int pool_thread_manage_r = pool_thread_manage(pool);
    if (pool_thread_manage_r != POOL_THREAD_SUCCESS) {
      PRINT_ERR("%s : %s", "pool_thread_manage",
          pool_thread_strerror(pool_thread_manage_r));
      goto error;
    }

    //read daemon pipe to get client pipe name
    PRINT_INFO("%s", STR_READ_PIPE_INFORMATIONS);
    int pipe_read_r = pipe_read(daemon_pipe_fd, client_pipe);
    if (pipe_read_r != PIPE_SUCCESS) {
      PRINT_ERR("%s : %d", "pipe_read", pipe_read_r);
      goto error;
    }
    PRINT_INFO("%s : %s", STR_PIPE_GET_NAME, client_pipe);
    PRINT_MSG("%s", STR_NEW_CONNECTION);

    if (client_pipe_fd != -1) {
      //close client pipe
      if (close(client_pipe_fd) != 0) {
        PRINT_ERR("%s : %s", "close", strerror(errno));
        goto error;
      }
    }

    //open client pipe
    int client_pipe_fd = open(client_pipe, O_WRONLY);
    if (client_pipe_fd == -1) {
      PRINT_ERR("%s : %s", "open", strerror(errno));
    } else {
      //get a thread shm name
      int pool_thread_enroll_r = pool_thread_enroll(pool, shm_name);
      if (pool_thread_enroll_r == POOL_TRHEAD_NO_MORE_THREAD) {
          PRINT_WARN("%s : %s", "pool_thread_enroll",
              pool_thread_strerror(pool_thread_enroll_r));

          //send the client there is no threads
          if (write(client_pipe_fd, RST, strlen(RST) + 1) == -1) {
            PRINT_ERR("%s : %s", "write", strerror(errno));
            return EXIT_FAILURE;
          }

      } else {
        if (pool_thread_enroll_r != POOL_THREAD_SUCCESS) {
          PRINT_WARN("%s : %s", "pool_thread_enroll",
              pool_thread_strerror(pool_thread_enroll_r));
          goto error;
        }

        //send it in the client pipe
        //open client pipe
        int client_pipe_fd = open(client_pipe, O_WRONLY);
        if (client_pipe_fd == -1) {
          PRINT_ERR("%s : %s", "open", strerror(errno));
          goto error;
        }

        //send the message
        if (write(client_pipe_fd, shm_name, strlen(shm_name) + 1) == -1) {
          PRINT_ERR("%s : %s", "write", strerror(errno));
          goto error;
        }
      }
    }

    if (close(daemon_pipe_fd) != 0) {
      PRINT_ERR("%s : %s", "close", strerror(errno));
      goto error;
    }
  }
  r = EXIT_SUCCESS;
  goto dispose;
error:
  r = EXIT_FAILURE;
dispose:
  PRINT_INFO("%s", STR_DISPOSE);
  //dispose daemon pipe
  int pipe_dispose_r = pipe_dispose();
  if (pipe_dispose_r != PIPE_SUCCESS) {
    PRINT_ERR("%s : %d", "pipe_dispose", pipe_dispose_r);
    r = EXIT_FAILURE;
  }
  //close daemon pipe
  if (daemon_pipe_fd != -1) {
    if (close(daemon_pipe_fd) == -1) {
      PRINT_ERR("%s : %s", "close", strerror(errno));
      r = EXIT_FAILURE;
    }
  }
  //dispose threads
  int pool_thread_dispose_r = pool_thread_dispose(&pool);
  if (pool_thread_dispose_r != POOL_THREAD_SUCCESS) {
    PRINT_ERR("%s : %s", "pool_thread_dispose",
        pool_thread_strerror(pool_thread_dispose_r));
    r = EXIT_FAILURE;
  }

  return r;
}

void monitor_signal(int signum) {
  if (signum < 0) {
    PRINT_ERR("%s : %d", "signal error", signum);
    exit(EXIT_FAILURE);
  }
  should_be_killed = true;
}
