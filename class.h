
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

// some kind of common value def struct for variables / return values??

struct variable_definition{
  struct variable_definition *next;
  const char *read_access;
  const char *write_access;
  const char *type;
  const char *name;
  const char *dimensions;
  // type
  // array definition
};

struct script_definition{
  const char *name;
  const char *signature;
  const char *external_name;
  const char *library;
  struct script_definition *next;
  // return type
  // arguments
  struct variable_definition *local_variables;
};

struct class_definition{
  const char *name;
  const char *ancestor;
  const char *parent;
  struct class_definition *next;
  struct script_definition *scripts;
  struct variable_definition *instance_variables;
};

struct class_group{
  // referenced types...?
  struct enumeration *enumerations;
  struct class_definition *classes;
  struct variable_definition *global_variables;
};

struct lib_entry;

struct class_group *class_parse(struct lib_entry *entry);
void class_free(struct class_group *class_group);

#endif
