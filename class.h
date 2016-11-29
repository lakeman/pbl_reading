
#ifndef class_header
#define class_header

#include <stdint.h>

struct enum_value{
  const char *name;
  uint16_t value;
};

struct enumeration{
  struct enumeration *next;
  const char *name;
  unsigned value_count;
  struct enum_value values[0];
};

struct script_definition{
  struct script_definition *next;
  const char *name;
};

struct class_definition{
  struct class_definition *next;
  struct script_definition *scripts;
};

struct class_group{
  struct enumeration *enumerations;
  struct class_definition *classes;
};

struct lib_entry;

struct class_group *class_parse(struct lib_entry *entry);
void class_free(struct class_group *class_group);

#endif
