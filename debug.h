
#ifndef debug_header
#define debug_header

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>

void _dump(const char *file, int line, const uint8_t *data, size_t len);

#define DEBUGF(FMT, ...) fprintf(stderr, "%s:%u - " FMT "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DUMP(B,L) _dump(__FILE__,__LINE__,(B),(L))
#define UNUSED(x) x __attribute__((__unused__))

#endif
