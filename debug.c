#include "debug.h"

void _dump(const char *file, int line, const uint8_t *data, size_t len){
  size_t i;
  for(i = 0; i < len; i += 16) {
    fprintf(stderr, "%s:%u - %04zx :", file, line, i);
    int j;
    for (j = 0; j < 16 && i + j < len; j++)
      fprintf(stderr, " %02x", data[i + j]);
    for (; j < 16; j++)
      fprintf(stderr, "   ");
    fprintf(stderr, "    ");
    for (j = 0; j < 16 && i + j < len; j++)
      fprintf(stderr, "%c", data[i+j] >= ' ' && data[i+j] < 0x7f ? data[i+j] : '.');
    fprintf(stderr, "\n");
  }
}
