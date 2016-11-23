
#ifndef debug_header
#define debug_header

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

void _dump(const char *file, int line, const uint8_t *data, size_t len, unsigned width);

#define DEBUGF(FMT, ...) fprintf(stderr, "%s:%u - " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DUMP(B,L) _dump(__FILE__,__LINE__,(uint8_t*)(B),(L),16)
#define DUMP_STRUCT(S) _dump(__FILE__,__LINE__,(uint8_t*)&S,sizeof S,16)
#define DUMP_ARRAY(S,C) _dump(__FILE__,__LINE__,(uint8_t*)&S,C * sizeof S,sizeof S)
#define UNUSED(x) x __attribute__((__unused__))

#endif
