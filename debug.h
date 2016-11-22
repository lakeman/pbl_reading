
#ifndef debug_header
#define debug_header

#include <stdio.h>

#define DEBUGF(FMT, ...) fprintf(stderr, "%s:%u - " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define UNUSED(x) x __attribute__((__unused__))


#endif
