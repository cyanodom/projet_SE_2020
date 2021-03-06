#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>

#include "load_conf.h"
#include "macros.h"

#define FUN_SUCCESS 0
#define FUN_FAILURE -1
#define FUN_UNABLE_TO_OPEN -2

#define STR_CANNOT_BE_GREATER_THAN "ne peut être supérieur à"

#define MAX_THREAD_DEFAULT 100
#define MIN_THREAD_DEFAULT 10
#define MAX_CONNECT_PER_THREAD_DEFAULT 1
#define SHM_SIZE_DEFAULT 10

#define STR_LOAD_FAIL "Le programme n'a pas été en mesure de charger"          \
    " le fichier"
#define STR_UKN_ID "identifieur non reconnu !"
//_____________________________strcmp______________________________________
//  A reason from wich I dont know the origin makes my valgrind say me
//    that I make a conditionnal jump or move that depends on uninitialized
//    value if I dont do that :
#undef strcmp
//  This is the official strcmp for the glibc source code :
int strcmp (const char *p1, const char *p2) {
  const unsigned char *s1 = (const unsigned char *) p1;
  const unsigned char *s2 = (const unsigned char *) p2;
  unsigned char c1, c2;

  do
    {
      c1 = (unsigned char) *s1++;
      c2 = (unsigned char) *s2++;
      if (c1 == '\0')
	return c1 - c2;
    }
  while (c1 == c2);

  return c1 - c2;
}
//----------------------------------------------------------

static void load_conf__default(size_t *max_thread_nb, size_t *min_thread_nb,
    size_t *max_connect_per_thread, size_t *shm_size);
static int load_conf__read_file(size_t *max_thread_nb, size_t *min_thread_nb,
    size_t *max_connect_per_thread, size_t *shm_size);

int load_conf_file(size_t *max_thread_nb, size_t *min_thread_nb,
    size_t *max_connect_per_thread, size_t *shm_size) {

  load_conf__default(max_thread_nb, min_thread_nb, max_connect_per_thread,
    shm_size);

  int read_file_ret = load_conf__read_file(max_thread_nb, min_thread_nb,
      max_connect_per_thread, shm_size);
  if (read_file_ret == FUN_FAILURE) {
    return LOAD_CONF_FAILURE;
  }
  if (read_file_ret == FUN_UNABLE_TO_OPEN) {
    load_conf__default(max_thread_nb, min_thread_nb, max_connect_per_thread,
      shm_size);
  }

  if (min_thread_nb != NULL && max_thread_nb != NULL) {
    if (*min_thread_nb > *max_thread_nb) {
      PRINT_ERR("%s : %s", "load_conf_file",
          "STR_MIN_THREAD "  STR_CANNOT_BE_GREATER_THAN " STR_MAX_THREAD");
      return LOAD_CONF_FAILURE;
    }
  }

  return LOAD_CONF_SUCCESS;
}

int load_conf__read_file(size_t *max_thread_nb, size_t *min_thread_nb,
    size_t *max_connect_per_thread, size_t *shm_size) {
  long int buffer_size;

  int fd = open(CONF_FILE_NAME, O_RDONLY);
  if (fd == -1) {
    PRINT_WARN("%s : %s", CONF_FILE_NAME, strerror(errno));
    return FUN_UNABLE_TO_OPEN;
  }

  struct stat fstat_struct;
  int fstat_ret = fstat(fd, &fstat_struct);
  if (fstat_ret == -1) {
    errno = 0;
    PRINT_WARN("%s : %s : %s", "load_conf__read_file", "fstat",
        strerror(errno));
    buffer_size = 1024;
  } else {
    buffer_size = fstat_struct.st_size;
  }

  char buf[buffer_size + 1];
  long int n;
  while ((n = read(fd, buf, (size_t) buffer_size)) > 0) {
    buf[n] = 0;
    long int indexBuf = 0;
    size_t state = 0;
    size_t value = 0;
    bool canStop = true;
    while (indexBuf < buffer_size) {
      long int indexStr = 0;
      char str[WORD_LEN_MAX + 1];
      while (indexBuf < n) {
        switch (state) {
        case 0: //on a le droit à autant d'espaces que voulu
          if  (!isspace(buf[indexBuf])) {
            if (isupper(buf[indexBuf]) || buf[indexBuf] == '_') {
              str[indexStr] = buf[indexBuf];
              ++indexStr;
              if (indexStr == WORD_LEN_MAX) {
                return FUN_FAILURE;
              }
              ++state;
            } else if (buf[indexBuf] == '#') {
              state += 10;
            } else {
              return FUN_FAILURE;
            }
          }
          break;
        case 1: //on récupère un identifier
          if (isupper(buf[indexBuf]) || buf[indexBuf] == '_') {
            str[indexStr] = buf[indexBuf];
            canStop = false;
            ++indexStr;
          } else {
            if  (isspace(buf[indexBuf])) {
              str[indexStr] = 0;
              ++state;
            } else if (buf[indexBuf] == '=') {
              str[indexStr] = 0;
              state += 2;
            } else if (buf[indexBuf] == '#') {
              str[indexStr] = 0;
              state += 11; //et on passe au prochain
            } else {
              return FUN_FAILURE;
            }
          }
          break;
        case 2: //on récupère le égal
          if (buf[indexBuf] == '#') {
            state += 10;
          } else if  (!isspace(buf[indexBuf])) {
            if (buf[indexBuf] == '=') {
              ++state;
            } else {
              return FUN_FAILURE;
            }
          }
          break;
        case 3: //on peut à nouveau avoir des espaces
          if (isdigit(buf[indexBuf])) {
            ++state;
            value = 10 * value + (size_t)(buf[indexBuf] - '0');
          } else if (!isspace(buf[indexBuf])) {
            if (buf[indexBuf] == '#') {
              state += 10;
            } else {
              return FUN_FAILURE;
            }
          }
          break;
        case 4: //on récupère la valeure
          if (isdigit(buf[indexBuf])) {
            value = 10 * value + (size_t)(buf[indexBuf] - '0');
          } else {
            str[indexStr] = 0;
            if (isspace(buf[indexBuf])) {
              if (strcmp(STR_MIN_THREAD, str) == 0) {
                if (min_thread_nb != NULL) {
                  *min_thread_nb = value;
                }
              } else if (strcmp(STR_MAX_THREAD, str) == 0) {
                if (max_thread_nb != NULL) {
                  *max_thread_nb = value;
                }
              } else if (strcmp(STR_MAX_CONNECT_PER_THREAD, str) == 0) {
                if (max_connect_per_thread != NULL) {
                  *max_connect_per_thread = value;
                }
              } else if (strcmp(STR_SHM_SIZE, str) == 0) {
                if (shm_size != NULL) {
                  *shm_size = value;
                }
              } else {
                PRINT_WARN("%s : %s : %s : %s", "load_conf__read_file",
                  CONF_FILE_NAME, STR_UKN_ID, str);
                return FUN_FAILURE;
              }
              indexStr = 0;
              canStop = true;
              value = 0;
              if (buf[indexBuf] == '\n') {
                state = 0;
              } else {
                ++state;
              }
            } else if (buf[indexBuf] == '#') {
              state += 10;
            } else {
               if (!isspace(buf[indexBuf])) {
                return FUN_FAILURE;
              }
            }
          }
          break;
        case 5: //on peut à nouveau avoir des espaces
          if (buf[indexBuf] == '\n') {
            state = 0;
          } else if (!isspace(buf[indexBuf])) {
            if (buf[indexBuf] == '#') {
              state += 10;
            } else {
              return FUN_FAILURE;
            }
          }
          break;
        case 6: //aller j'usqu'a la fin de la ligne
          state = 0;
          break;
        default: //commentaire
          if (buf[indexBuf] == '\n') {
            state -= 9;
          }
          break;
        }
        ++indexBuf;
      }
      if (!canStop) {
        return FUN_FAILURE;
      }
    }
  }

  if (n == -1) {
    PRINT_ERR("%s : %s : %s", "load_conf__read_file", "read", strerror(errno));
    return FUN_FAILURE;
  }

  if (close(fd) == -1) {
    PRINT_ERR("%s : %s : %s", "load_conf__read_file", "close", strerror(errno));
    return FUN_FAILURE;
  }

  return FUN_SUCCESS;
}

void load_conf__default(size_t *max_thread_nb, size_t *min_thread_nb,
    size_t *max_connect_per_thread, size_t *shm_size) {
  if (max_thread_nb != NULL) {
    *max_thread_nb = MAX_THREAD_DEFAULT;
  }
  if (min_thread_nb != NULL) {
    *min_thread_nb = MIN_THREAD_DEFAULT;
  }
  if (max_connect_per_thread != NULL) {
    *max_connect_per_thread = MAX_CONNECT_PER_THREAD_DEFAULT;
  }
  if (shm_size != NULL) {
    *shm_size = SHM_SIZE_DEFAULT;
  }
}
