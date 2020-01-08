#define PRINT_DEBUG(FORMAT, DEBUG_LEVEL, ...) fprintf(stderr, "*** %s" __FILE__ \
    " : " FORMAT "\n", DEBUG_LEVEL, __VA_ARGS__);

#define PRINT_ERR(FORMAT, ...) PRINT_DEBUG(FORMAT, "ERROR : ", __VA_ARGS__)
#define PRINT_WARN(FORMAT, ...) PRINT_DEBUG(FORMAT, "warning : ", __VA_ARGS__)
#define STR(X) #X
#define XSTR(X) STR(STR)

#define WORD_LEN_MAX 63
