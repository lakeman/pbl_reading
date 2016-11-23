#include <assert.h>
#include <string.h>
#include "pool_alloc.h"
#include "class.h"
#include "lib.h"
#include "pb_class_types.h"
#include "debug.h"

struct data_table{
  uint32_t data_length;
  unsigned metadata_count;
  uint8_t *data;
  struct pbtable_info *metadata;
};

struct type_defs{
  struct data_table table;
  unsigned count;
  struct pbtype_def *types;
};

struct script_def_private{
  struct script_definition pub;
  uint16_t code_size;
  uint8_t *code;
  uint16_t debugline_count;
  struct pbdebug_line_num *debug_lines;
  struct type_defs local_variables;
  struct data_table references;
};

struct class_def_private{
  struct class_definition pub;
  struct pbclass_header *header;
  struct pbscript_short_header *short_headers;
  struct type_defs variables; //? better name ?
  struct type_defs instance_variables;
  struct pbvalue *instance_values;
  struct pbindirect_ref *indirect_refs;
  struct pbscript_header *script_headers;
};

struct class_group_private{
  struct class_group pub;
  struct pool *pool;
  struct pbfile_header header;
  uint16_t ext_ref_count;
  struct pbext_reference *external_refs;
  struct data_table main_table;
  struct type_defs main_types;
  uint16_t type_count;
  uint16_t class_count;
  struct data_table function_name_table;
  struct data_table arguments_table;
  struct type_defs type_list;
  struct type_defs enum_values;
  // TODO variable length based on version!
  struct pbtype_header *type_headers;
  struct pbclass_header *class_headers;
};

#define read_type(E,S) assert(lib_entry_read(E, (uint8_t *)&S, sizeof S)==sizeof S)

static void* read_block(struct lib_entry *entry, struct class_group_private *class_group, size_t length){
  if (length==0)
    return NULL;
  uint8_t *raw = pool_alloc(class_group->pool, length, 1);
  assert(lib_entry_read(entry, raw, length)==length);
  return (void*)raw;
}

#define read_array(E,CD,S,C) read_block(E,CD,S*C)
#define read_type_array(E,CL,S,C) S=read_array(E,CL,sizeof(*S),C)

static void read_table(struct lib_entry *entry, struct class_group_private *class_group, struct data_table *table){
  read_type(entry, table->data_length);
  uint32_t metadata_length;
  read_type(entry, metadata_length);
  DEBUGF("Table %x data, %x metadata", table->data_length, metadata_length);
  table->data = read_block(entry, class_group, table->data_length);
  //DUMP(table->data, table->data_length);
  table->metadata_count = metadata_length / sizeof(struct pbtable_info);
  table->metadata = (struct pbtable_info*)read_block(entry, class_group, metadata_length);
  //DUMP_ARRAY(*table->metadata, count);
}

static void read_type_defs(struct lib_entry *entry, struct class_group_private *class_group, struct type_defs *type_defs){
  read_table(entry, class_group, &type_defs->table);
  uint16_t size;
  read_type(entry, size);
  type_defs->count = size / sizeof(struct pbtype_def);
  type_defs->types = (struct pbtype_def*)read_block(entry, class_group, size);
}

static void read_expecting(struct lib_entry *entry, const uint16_t *expect, unsigned count){
  uint16_t data[count];
  assert(lib_entry_read(entry, (uint8_t*)&data, sizeof(data))==sizeof(data));
  assert(memcmp(expect, data, sizeof(data))==0);
}

struct class_group *class_parse(struct lib_entry *entry){
  struct pool *pool = pool_create();
  struct class_group_private *class_group = pool_alloc_struct(pool, struct class_group_private);
  class_group->pool = pool;

  read_type(entry, class_group->header);
  DEBUGF("header...");
  assert(class_group->header.compiler_version >= PB6);

  read_type(entry, class_group->ext_ref_count);
  DEBUGF("%u references", class_group->ext_ref_count);
  read_type_array(entry, class_group, class_group->external_refs, class_group->ext_ref_count);
  read_table(entry, class_group, &class_group->main_table);

  static uint16_t expect1[] = {0x10,0x32,0x08};
  read_expecting(entry, expect1, 3);

  read_type_defs(entry, class_group, &class_group->main_types);

  read_type(entry, class_group->type_count);
  read_type(entry, class_group->class_count);
  DEBUGF("%u types & %u classes", class_group->type_count, class_group->class_count);

  read_table(entry, class_group, &class_group->function_name_table);
  read_table(entry, class_group, &class_group->arguments_table);

  static uint16_t expect2[] = {0x0a,0x78,0x11};
  read_expecting(entry, expect2, 3);

  read_type_defs(entry, class_group, &class_group->type_list);

  static uint16_t expect3[] = {0x14,0xf0,0x11};
  read_expecting(entry, expect3, 3);

  read_type_defs(entry, class_group, &class_group->enum_values);

  read_type_array(entry, class_group, class_group->type_headers, class_group->type_count);
  read_type_array(entry, class_group, class_group->class_headers, class_group->class_count);

  // now the hard(-ish) part....
  unsigned i, j=0;
  struct enumeration **enum_ptr = &class_group->pub.enumerations;
  struct class_definition **class_ptr = &class_group->pub.classes;

  for (i=0;i<class_group->type_count;i++){
    DEBUGF("Type %u", i);
    if ((class_group->type_headers[i].flags & 0xFF) == 3){
      // essentially in pbvmXX.dll(_typedef.grp) only
      unsigned count = class_group->type_headers[i].enum_count;

      struct pbenum_value values[count];
      read_type(entry, values);
      size_t size = sizeof(struct enumeration) + count * sizeof(struct enum_value);

      struct enumeration *enumeration = (*enum_ptr) = pool_alloc(class_group->pool, size,
	alignment_of(struct enumeration));

      memset(enumeration, size, 0);
      enum_ptr = &enumeration->next;

      // TODO names
      //enumeration->name = ;
      enumeration->value_count = count;

      unsigned k;
      for (k=0;k<count;k++){
	//enumeration->values[k].name = ;
	enumeration->values[k].value = values[k].value;
      }

    }else if(class_group->type_headers[i].flags == 0x85 // _initsrc
	|| class_group->type_headers[i].flags == 0x89 // _sharsrc
	|| class_group->type_headers[i].flags == 0x0B // _globsrc
	){
      // NOOP
    }else{
      // class
      DEBUGF("Class %u", j);

      struct class_def_private *class_def = pool_alloc_struct(class_group->pool, struct class_def_private);
      memset(class_def, sizeof(*class_def), 0);
      (*class_ptr) = &class_def->pub;
      class_ptr = &class_def->pub.next;

      struct pbclass_header *cls_header = class_def->header = &class_group->class_headers[j++];

      uint16_t script_count;
      read_type(entry, script_count);
      struct pbscript_list scripts[script_count];
      read_type(entry, scripts);

      struct script_definition **script_ptr = &class_def->pub.scripts;
      unsigned k;
      for (k=0; k<script_count; k++){
	if (!scripts[k].implemented)
	  continue;

	DEBUGF("Script %u", k);
	struct script_def_private *script_def = pool_alloc_struct(class_group->pool, struct script_def_private);
	memset(script_def, sizeof(*script_def), 0);
	(*script_ptr) = &script_def->pub;
	script_ptr = &script_def->pub.next;

	read_type(entry, script_def->code_size);
	read_type(entry, script_def->debugline_count);
	uint16_t ignored;
	read_type(entry, ignored);

	script_def->code = read_block(entry, class_group, script_def->code_size);
	read_type_array(entry, class_group, script_def->debug_lines, script_def->debugline_count);

	static uint16_t expect4[] = {16,100,8};
	read_expecting(entry, expect4, 3);

	read_type_defs(entry, class_group, &script_def->local_variables);
	read_table(entry, class_group, &script_def->references);
      }

      read_type_array(entry, class_group, class_def->short_headers, cls_header->script_count);

      // absolutely no idea what this is;
      uint32_t ignored_array[cls_header->something_count];
      read_type(entry, ignored_array);

      static uint16_t expect5[] = {16,50,11};
      read_expecting(entry, expect5, 3);
      read_type_defs(entry, class_group, &class_def->variables);
      read_expecting(entry, expect5, 3);
      read_type_defs(entry, class_group, &class_def->instance_variables);
      read_type_array(entry, class_group, class_def->instance_values, cls_header->variable_count);
      read_type_array(entry, class_group, class_def->indirect_refs, cls_header->indirect_count);
      read_type_array(entry, class_group, class_def->script_headers, cls_header->script_count);
    }
  }

  // did we see all classes?
  assert(j==class_group->class_count);
  {
    // did we hit the end of the binary data?
    uint8_t ignored;
    assert(lib_entry_read(entry, &ignored, 1)==0);
  }


  return (struct class_group *)class_group;
}

void class_free(struct class_group *class_group){
  struct class_group_private *cls = (struct class_group_private *)class_group;
  pool_release(cls->pool);
}
