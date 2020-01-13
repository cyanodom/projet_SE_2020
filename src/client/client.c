#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdbool.h>

#include "macros.h"

#define STR_THE_PIPE "le pipe"
#define STR_WILL_BE_USED "sera uttilisé"
#define STR_CONNECTED "connecté au serveur\n"

#define CLIENT_PIPE_BASE_NAME "client_pipe"

#define FUN_MACRO_TO_LONG -3
#define FUN_SUCCESS 0
#define FUN_FAILURE -1

bool exists(char * pipe_name);
int find_pipe_name(char *pipe_name);
int generate_pipe_name(char *pipe_name, size_t id);

int main(void) {
  //find the name of the client pipe
  char client_pipe[WORD_LEN_MAX + 1];
  if (find_pipe_name(client_pipe) != FUN_SUCCESS) {
    return EXIT_FAILURE;
  }
  PRINT_INFO("%s %s %s", STR_THE_PIPE, client_pipe, STR_WILL_BE_USED);

  // open the daemon pipe to connect the daemon
  int daemon_fd = open(BASE_PIPE_NAME, O_WRONLY);
  if (daemon_fd == -1) {
    perror("open");
    return EXIT_FAILURE;
  }

  //send to the daemon the SYNC command
  size_t message = SYNC;
  if (write(daemon_fd, &message, sizeof(size_t)) == -1) {
    PRINT_ERR("%s : %s", "write", strerror(errno));
    return EXIT_FAILURE;
  }

  //and send to the daemon the name of the client pipe
  if (write(daemon_fd, client_pipe, strlen(client_pipe) + 1) == -1) {
    PRINT_ERR("%s : %s", "write", strerror(errno));
    return FUN_FAILURE;
  }

  close(daemon_fd);

  // open the client pipe
  int own_fd = open(client_pipe, O_RDONLY);
  if (own_fd == -1) {
    PRINT_ERR("%s : %s", "open", strerror(errno));
    return EXIT_FAILURE;
  }

  if (write(STDOUT_FILENO, STR_CONNECTED, strlen(STR_CONNECTED)) == -1) {
    PRINT_ERR("%s : %s", "write", strerror(errno));
    return EXIT_FAILURE;
  }
  //read in the client pipe
  char shm_name[WORD_LEN_MAX];
  size_t length = 0;
  char c;
  do {
    if (read(own_fd, &c, sizeof(char)) == -1) {
      PRINT_ERR("%s : %s", "read", strerror(errno));
      return EXIT_FAILURE;
    }
    shm_name[length] = c;
    ++length;
  } while (c != '\0' && length < WORD_LEN_MAX);
  printf("%s\n", shm_name);
  shm_open(shm_name, O_RDWR, S_IRUSR | S_IWUSR);

  return EXIT_SUCCESS;
}

int find_pipe_name(char *client_pipe) {
  //copy macro into client_pipe
  size_t str_len = strlen(CLIENT_PIPE_BASE_NAME) > WORD_LEN_MAX
      ? WORD_LEN_MAX : strlen(CLIENT_PIPE_BASE_NAME);
  size_t index;
  for (index = 0; index < str_len; ++index) {
    client_pipe[index] = CLIENT_PIPE_BASE_NAME[index];
  }

  //find the name
  size_t i = 0;
  do {
    int generate_pipe_name_r = generate_pipe_name(client_pipe, i);
    if (generate_pipe_name_r != FUN_SUCCESS) {
      printf("%d", generate_pipe_name_r);
      return FUN_FAILURE;
    }
    ++i;
  } while (exists(client_pipe));

  //try to create the client pipe
  if (mkfifo(client_pipe, S_IRUSR | S_IWUSR) == -1) {
    PRINT_ERR("%s : %s", "mkfifo", strerror(errno));
    return FUN_FAILURE;
  }
  return FUN_SUCCESS;
}

//---------------------------------TOOLS----------------------------------------

int generate_pipe_name(char *pipe_name, size_t id) {
  if (strlen(CLIENT_PIPE_BASE_NAME) + 1 < WORD_LEN_MAX) {
    char c = (char) ('0' + id);
    id = (id - (id % 10)) / 10;
    if (id == 0) {
      pipe_name[strlen(CLIENT_PIPE_BASE_NAME)] = c;
      pipe_name[strlen(CLIENT_PIPE_BASE_NAME) + 1] = 0;
      return FUN_SUCCESS;
    }
  } else {
    return FUN_MACRO_TO_LONG;
  }

  size_t i;
  for (i = strlen(CLIENT_PIPE_BASE_NAME); id != 0; ++i) {
    pipe_name[i] = (char) ('0' + (id % 10));
    id = (id - (id % 10)) / 10;
    if (id != 0 && strlen(CLIENT_PIPE_BASE_NAME) + 1 == WORD_LEN_MAX) {
      return FUN_FAILURE;
    }
  }
  //the null terminating string character
  pipe_name[i] = 0;
  return FUN_SUCCESS;
}

bool exists(char * pipe_name) {
  //I found some C++ libs using this code to do the same thing ...
  //I dont think it is the best way to do but I dont have found any other idea
  struct stat stats;
  return (stat(pipe_name, &stats) == 0);
}
