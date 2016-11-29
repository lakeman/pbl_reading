
#ifndef debug_header
#define debug_header

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

void _dump(const char *file, int line, const uint8_t *data, size_t len, unsigned width, int ascii);

#define DEBUG_LIB 0
#define DEBUG_ALLOC 0
#define DEBUG_RAWREAD 0
#define DEBUG_PARSE 1

#define DEBUGF(TYPE, FMT, ...) if (DEBUG_ ## TYPE) fprintf(stderr, "%s:%u " #TYPE " - " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DUMP(TYPE, B, L) if (DEBUG_ ## TYPE) _dump(__FILE__,__LINE__,(uint8_t*)(B),(L),16,1)
#define DUMP_STRUCT(TYPE, S) if (DEBUG_ ## TYPE) _dump(__FILE__,__LINE__,(uint8_t*)&S,sizeof S,16,0)
#define DUMP_ARRAY(TYPE, S,C) if (DEBUG_ ## TYPE) _dump(__FILE__,__LINE__,(uint8_t*)&S,C * sizeof S,sizeof S,0)
#define UNUSED(x) x __attribute__((__unused__))

#endif
