#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pipe.h"
#include "macros.h"

int pipe_create_base(int *fd) {
  if (mkfifo(BASE_PIPE_NAME, S_IRUSR | S_IWUSR) == -1) {
    PRINT_ERR("%s : %s : %s", "pipe_create_base", "mkfifo", strerror(errno));
    return PIPE_FAILURE;
  }

  *fd = open(BASE_PIPE_NAME, O_RDONLY);
  return PIPE_SUCCESS;
}

int pipe_dispose(int fd) {
  if (unlink(BASE_PIPE_NAME) == -1) {
    PRINT_ERR("%s : %s : %s", "pipe_dispose", "unlink", strerror(errno));
    return PIPE_FAILURE;
  }

  if (close(fd) == -1) {
    PRINT_ERR("%s : %s : %s", "pipe_dispose", "close", strerror(errno));
    return PIPE_FAILURE;
  }
  return PIPE_SUCCESS;
}
