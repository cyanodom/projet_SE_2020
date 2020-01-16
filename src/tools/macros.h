#ifndef MACRO__H
  #define MACRO__H
  #include <stdio.h>

  #ifndef DEBUG_LEVEL
    #define DEBUG_LEVEL 0
  #endif

  #define PRINT_DEBUG(THIS_DEBUG_LEVEL, FORMAT, ...)                           \
    if (THIS_DEBUG_LEVEL <= DEBUG_LEVEL) {                                     \
      if (THIS_DEBUG_LEVEL == 3) {                                             \
        fprintf(stdout, "--- %s : " FORMAT "\n", "INFO", __VA_ARGS__);         \
      } else if (THIS_DEBUG_LEVEL == 2) {                                      \
        fprintf(stderr, "*** %s : %s : " FORMAT "\n", "WARNING", __func__,     \
            __VA_ARGS__);                                                      \
      } else if (THIS_DEBUG_LEVEL == 1) {                                      \
        fprintf(stderr, "!!! %s : %s(%d) : %s : " FORMAT "\n", "ERROR",        \
            __FILE__, __LINE__, __func__, __VA_ARGS__);                        \
      } else if (THIS_DEBUG_LEVEL == 0) {                                      \
        fprintf(stdout, "-> " FORMAT "\n", __VA_ARGS__);                       \
      }                                                                        \
    }

  #define PRINT_MSG(FORMAT, ...) PRINT_DEBUG(0, FORMAT, __VA_ARGS__)
  #define PRINT_ERR(FORMAT, ...) PRINT_DEBUG(1, FORMAT, __VA_ARGS__)
  #define PRINT_WARN(FORMAT, ...) PRINT_DEBUG(2, FORMAT, __VA_ARGS__)
  #define PRINT_INFO(FORMAT, ...) PRINT_DEBUG(3, FORMAT, __VA_ARGS__)
  #define STR(X) #X
  #define XSTR(X) STR(STR)

  #define WORD_LEN_MAX 63
  #define BASE_PIPE_NAME "daemon_pipe"
  #define SYNC "SYNC"
  #define RST "RST"
  #define END "END"
#endif
