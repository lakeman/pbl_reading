
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

struct variable_definition{
  struct variable_definition *next;
  const char *read_access;
  const char *write_access;
  const char *type;
  const char *name;
  const char *dimensions;
  // TODO indirect variables
  uint8_t constant:1;
  uint8_t user_defined:1;
};

struct argument_definition{
  struct argument_definition *next;
  const char *access;
  const char *type;
  const char *name;
  const char *dimensions;
};

struct script_definition{
  struct script_definition *next;
  const char *name;
  const char *access;
  const char *signature;
  const char *external_name;
  const char *library;
  const char *return_type;
  const char *event_type;
  unsigned local_variable_count;
  struct variable_definition *local_variables;
  unsigned argument_count;
  struct argument_definition *arguments;
  uint8_t event:1;
  uint8_t hidden:1;
  uint8_t system:1;
  uint8_t rpc:1;
  uint8_t implemented:1;
};

struct class_definition{
  struct class_definition *next;
  const char *name;
  const char *ancestor;
  const char *parent;
  struct script_definition *scripts;
  unsigned instance_variable_count;
  struct variable_definition *instance_variables;
  uint8_t autoinstantiate:1;
  // TODO initial values
};

struct class_group{
  struct enumeration *enumerations;
  struct class_definition *classes;
  unsigned global_variable_count;
  struct variable_definition *global_variables;
  // TODO type references
};

struct lib_entry;

struct class_group *class_parse(struct lib_entry *entry);
void class_free(struct class_group *class_group);

#endif
