
#ifndef lib_header
#define lib_header

#include <stdint.h>
#include <unistd.h>

struct library{
  uint8_t unicode;
  const char *filename;
  const char *version;
  const char *comment;
  uint32_t timestamp;
  uint16_t filetype;
};

struct lib_entry{
  size_t length;
  uint32_t timestamp;
  const char *name;
  const char *comment;
};

struct library *lib_open(const char *filename);
void lib_close(struct library *lib);

typedef void (*entry_callback) (struct lib_entry *entry, void *context);

struct lib_entry *lib_find(struct library *lib, const char *entry_name);
void lib_enumerate(struct library *lib, entry_callback callback, void *context);

size_t lib_entry_read(struct lib_entry *entry, uint8_t *buffer, size_t len);

#endif
