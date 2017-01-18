
#ifndef class_header
#define class_header

#include <stdint.h>
#include <stdio.h>

struct enum_value{
  const char *name;
  uint16_t value;
};

struct enumeration{
  unsigned value_count;
  struct enum_value values[0];
};

struct variable_definition{
  const char *read_access;
  const char *write_access;
  const char *type;
  const char *name;
  const char *dimensions;
  unsigned value_count;
  const char **initial_values;
  // descriptors
  uint8_t indirect:1;
  uint8_t constant:1;
  uint8_t user_defined:1;
};

struct argument_definition{
  const char *access;
  const char *type;
  const char *name;
  const char *dimensions;
};

struct script_definition{
  const char *name;
  const char *access;
  const char *signature;
  const char *external_name;
  const char *library;
  const char *return_type;
  const char *event_type;
  unsigned local_variable_count;
  struct variable_definition **local_variables;
  unsigned argument_count;
  struct argument_definition **arguments;
  unsigned throws_count;
  const char **throws;
  uint8_t event:1;
  uint8_t hidden:1;
  uint8_t system:1;
  uint8_t rpc:1;
  uint8_t implemented:1;
  uint8_t in_ancestor:1;
};

struct class_definition{
  const char *ancestor;
  const char *parent;
  unsigned script_count;
  struct script_definition **scripts;
  unsigned instance_variable_count;
  struct variable_definition **instance_variables;
  uint8_t autoinstantiate:1;
  // TODO initial values
};

enum type_enum{
  enum_type,
  class_type,
  initsrc,
  sharedsrc,
  globalsrc
};

struct type_definition{
  enum type_enum type;
  const char *name;
  union{
    struct class_definition *class_definition;
    struct enumeration *enum_definition;
  };
};

struct class_group{
  unsigned global_variable_count;
  struct variable_definition **global_variables;
  unsigned type_count;
  struct type_definition *types;
};

struct lib_entry;

struct class_group *class_parse(struct lib_entry *entry);
void class_free(struct class_group *class_group);

void dump_script_resources(FILE *fd, struct class_group *group, struct script_definition *script);

#endif
