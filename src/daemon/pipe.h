#ifndef PIPE__H
#define PIPE__H

#define PIPE_FAILURE -1
#define PIPE_SUCCESS 0

extern int pipe_dispose(int fd);

extern int pipe_create_base(int *fd);

#endif
