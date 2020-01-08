#ifndef LOAD_FILE__H
#define LOAD_FILE__H

#include <stddef.h>

#define LOAD_CONF_SUCCESS 0
#define LOAD_CONF_FAILURE -1

#define CONF_FILE_NAME "daemon.conf"

//  load_file : lis le fichier représenté par la constante CONF_FILE_NAME
extern int load_conf_file(size_t *max_thread_nb, size_t *min_thread_nb,
    size_t *max_connect_per_thread, size_t *shm_size);

#endif
