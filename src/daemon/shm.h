#ifndef SHM__H
#define SHM__H

  #include <stddef.h>
  #include <semaphore.h>

  #define SHM_HEADER (sizeof(sem_t) + sizeof(sem_t) + sizeof(char *))

  typedef struct shared_memory {
    sem_t thread_send;
    sem_t client_send;
    char data[];
  } shared_memory;

#endif
