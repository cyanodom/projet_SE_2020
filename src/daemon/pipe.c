#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pipe.h"
#include "macros.h"

int pipe_create_base() {
  if (mkfifo(BASE_PIPE_NAME, S_IRUSR | S_IWUSR) == -1) {
    PRINT_ERR("%s : %s", "mkfifo", strerror(errno));
    return PIPE_FAILURE;
  }
  return PIPE_SUCCESS;
}

int pipe_read(int fd, char *pipe_name) {//TODO read by buffer
  char message[WORD_LEN_MAX];
  size_t i = 0;
  ssize_t err;
  while ((err = read(fd, &message[i], sizeof(char))) > 0) {
    if (message[i] == 0) {
      break;
    }
    ++i;
  }
  if (err == -1) {
    PRINT_ERR("%s : %s", "read", strerror(errno));
    return PIPE_FAILURE;
  }

  if (strcmp(SYNC, message) != 0) {
    return PIPE_FAILURE;
  }

  size_t length = 0;
  char c;
  do {
    if (read(fd, &c, sizeof(char)) == -1) {
      PRINT_ERR("%s : %s", "read", strerror(errno));
      return PIPE_FAILURE;
    }
    pipe_name[length] = c;
    ++length;
  } while (c != '\0' && length < WORD_LEN_MAX);
  return PIPE_SUCCESS;
}

int pipe_dispose() {
  if (unlink(BASE_PIPE_NAME) == -1) {
    PRINT_ERR("%s : %s", "unlink", strerror(errno));
    return PIPE_FAILURE;
  }
  return PIPE_SUCCESS;
}
