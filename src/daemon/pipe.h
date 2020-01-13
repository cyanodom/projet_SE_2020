#ifndef PIPE__H
  #define PIPE__H

  #define PIPE_FAILURE -1
  #define PIPE_SUCCESS 0
  #define PIPE_ERROR_CLIENT -2

  extern int pipe_dispose();
  extern int pipe_read(int fd, char *pipeName);
  extern int pipe_create_base();

#endif
