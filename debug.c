#include "debug.h"

void _dump(const char *file, int line, const uint8_t *data, size_t len, unsigned width){
  size_t i;
  for(i = 0; i < len; i += width) {
    fprintf(stderr, "%s:%u - %04zx :", file, line, i);
    unsigned j;
    for (j = 0; j < width && i + j < len; j++)
      fprintf(stderr, " %02x", data[i + j]);
    for (; j < width; j++)
      fprintf(stderr, "   ");
    fprintf(stderr, "    ");
    for (j = 0; j < width && i + j < len; j++)
      fprintf(stderr, "%c", data[i+j] >= ' ' && data[i+j] < 0x7f ? data[i+j] : '.');
    fprintf(stderr, "\n");
  }
}
