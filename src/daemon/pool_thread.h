#ifndef POOL_THREAD__H
#define POOL_THREAD__H

#include <pthread.h>

#define POOL_THREAD_SUCCESS 0
#define POOL_THREAD_FAILURE -1
#define POOL_TRHEAD_NO_MORE_THREAD -2

extern int pool_thread_init(pthread_t **threads, size_t min_thread_nb,
    size_t max_thread_nb);

#endif
