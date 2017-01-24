#include "debug.h"

void _dump(FILE *f, const char *file, int line, const uint8_t *data, size_t len, unsigned width, int ascii){
  size_t i;
  unsigned row=0;
  for(i = 0; i < len; i += width) {
    if (file)
      fprintf(f, "%s:%u - ", file, line);
    fprintf(f, "%04zx (%u):", i, row++);
    unsigned j;
    for (j = 0; j < width && i + j < len; j++)
      fprintf(f, " %02x", data[i + j]);
    if (ascii){
      for (; j < width; j++)
	fprintf(f, "   ");
      fprintf(f, "    ");
      for (j = 0; j < width && i + j < len; j++)
	fprintf(f, "%c", data[i+j] >= ' ' && data[i+j] < 0x7f ? data[i+j] : '.');
    }
    fprintf(f, "\n");
  }
}
