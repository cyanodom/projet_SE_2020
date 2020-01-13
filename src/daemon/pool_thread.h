#ifndef POOL_THREAD__H
  #define POOL_THREAD__H

  #include <pthread.h>

  #define POOL_THREAD_SUCCESS 0
  #define POOL_THREAD_FAILURE -1
  #define POOL_TRHEAD_NO_MORE_THREAD -2

  typedef struct pool_thread pool_thread;

  extern int pool_thread_init(pool_thread **pool, size_t min_thread_nb,
      size_t max_thread_nb, size_t max_connect_per_thread, size_t shm_size);

  extern int pool_thread_dispose(pool_thread **pool);

  extern int pool_thread_enroll(pool_thread *pool, char *shm_name);

  extern int pool_thread_manage(pool_thread *pool);
#endif
