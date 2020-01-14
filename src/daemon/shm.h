#ifndef SHM__H
#define SHM__H

  #include <stddef.h>
  #include <semaphore.h>

  #define SHM_HEADER (sizeof(sem_t) + sizeof(sem_t))

  typedef struct shared_memory {
    sem_t thread_send;
    sem_t client_send;
    char data[];
  } shared_memory;

  shared_memory **shm_obj = NULL;

#endif
