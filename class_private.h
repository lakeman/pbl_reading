#ifndef class_private_header
#define class_private_header

#include <stdint.h>
#include "class.h"
#include "pb_class_types.h"

struct data_table{
  uint32_t data_length;
  unsigned metadata_count;
  const uint8_t *data;
  const struct pbtable_info *metadata;
};

struct type_defs{
  struct data_table table;
  unsigned count;
  const struct pbtype_def *types;
  const char **names;
};

struct variable_def_private{
  struct variable_definition pub;
  const int32_t *dimensions;
};

struct arg_def_private{
  struct argument_definition pub;
  const int32_t *dimensions;
};

struct script_implementation{
  uint16_t number;
  uint16_t code_size;
  const uint8_t *code;
  uint16_t debugline_count;
  const struct pbdebug_line_num *debug_lines;
  struct type_defs local_variables;
  struct data_table resources;
};

struct script_def_private{
  struct script_definition pub;
  struct script_implementation *body;
  const struct pbscript_short_header *short_header;
  const struct pbscript_header *header;
  const struct pbarg_def *arguments;
  const struct pbtable_info *argument_info;
  const uint16_t *throw_types;
};

struct class_def_private{
  struct class_definition pub;
  const struct pbtype_header *type_header;
  const struct pbclass_header *header;
  struct type_defs imports;
  struct type_defs instance_variables;
  const struct pbvalue *instance_values;
  const struct pbindirect_ref *indirect_refs;
};

struct class_group_private{
  struct class_group pub;
  struct pool *pool;
  struct pbfile_header header;
  uint16_t ext_ref_count;
  const struct pbext_reference *external_refs;
  struct data_table main_table;
  const char **ref_names;
  struct type_defs global_types;
  uint16_t class_count;
  struct data_table function_name_table;
  struct data_table arguments_table;
  struct type_defs type_list; // main type list
  struct type_defs enum_values;
  const struct pbtype_header *type_headers;
};

const void *get_table_ptr(struct class_group_private *class_group, struct data_table *table, uint32_t offset);
const struct pbtable_info *get_table_info(struct class_group_private *class_group, struct data_table *table, uint32_t offset);
const char *get_table_string(struct class_group_private *class_group, struct data_table *table, uint32_t offset);
struct class_def_private *get_class_by_type(struct class_group_private *class_group, uint16_t type);
const char *get_type_name(struct class_group_private *class_group, uint16_t type);

#endif
