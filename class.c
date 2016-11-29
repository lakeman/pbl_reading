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
  const char **names;
};

struct script_implementation{
  uint16_t number;
  uint16_t code_size;
  uint8_t *code;
  uint16_t debugline_count;
  struct pbdebug_line_num *debug_lines;
  struct type_defs local_variables;
  struct data_table references;
};

struct script_def_private{
  struct script_definition pub;
  struct pbscript_short_header *short_header;
  struct pbscript_header *header;
  struct script_implementation *body;
};

struct class_def_private{
  struct class_definition pub;
  struct pbclass_header *header;
  struct type_defs variables; //? better name ?
  struct type_defs instance_variables;
  struct pbvalue *instance_values;
  struct pbindirect_ref *indirect_refs;
};

struct class_group_private{
  struct class_group pub;
  struct pool *pool;
  struct pbfile_header header;
  uint16_t ext_ref_count;
  struct pbext_reference *external_refs;
  struct data_table main_table;
  const char **ref_names;
  struct type_defs global_types;
  uint16_t type_count;
  uint16_t class_count;
  struct data_table function_name_table;
  struct data_table arguments_table;
  struct type_defs type_list; // main type list
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
  DEBUGF(PARSE, "Table %x data, %x metadata", table->data_length, metadata_length);
  table->data = read_block(entry, class_group, table->data_length);
  //DUMP(table->data, table->data_length);
  table->metadata_count = metadata_length / sizeof(struct pbtable_info);
  table->metadata = (struct pbtable_info*)read_block(entry, class_group, metadata_length);
  //DUMP_ARRAY(*table->metadata, count);

  // TODO use metadata to detect the gaps between structures (where unicode strings are located)
  // and convert to utf8 in place?
}

static const char *get_table_string(struct class_group_private *class_group, struct data_table *table, uint32_t offset){
  // TODO if offset & 0x80000000, table = class_group->main_table
  assert(offset < table->data_length);
  if (class_group->header.compiler_version<PB10)
    return (const char *)&table->data[offset];
  return pool_dup_u(class_group->pool, (const UChar *)&table->data[offset]);
}

static void read_type_defs(struct lib_entry *entry, struct class_group_private *class_group, struct type_defs *type_defs){
  read_table(entry, class_group, &type_defs->table);
  uint16_t size;
  read_type(entry, size);
  type_defs->count = size / sizeof(struct pbtype_def);
  read_type_array(entry, class_group, type_defs->types, type_defs->count);
  type_defs->names = pool_alloc_array(class_group->pool, const char *, type_defs->count);
  unsigned i;
  for (i=0;i<type_defs->count;i++){
    type_defs->names[i]=get_table_string(class_group, &type_defs->table, type_defs->types[i].name_offset);
    DEBUGF(PARSE, "Type[%u] %s", i, type_defs->names[i]);
  }
}

static void read_expecting(struct lib_entry *entry, const uint16_t *expect, unsigned count){
  uint16_t data[count];
  assert(lib_entry_read(entry, (uint8_t*)&data, sizeof(data))==sizeof(data));
  assert(memcmp(expect, data, sizeof(data))==0);
}

struct class_group *class_parse(struct lib_entry *entry){
  struct pool *pool = pool_create();
  struct class_group_private *class_group = pool_alloc_type(pool, struct class_group_private);
  memset(class_group, sizeof(*class_group), 0);

  class_group->pool = pool;

  read_type(entry, class_group->header);
  DEBUGF(PARSE, "header...");
  assert(class_group->header.compiler_version >= PB6);

  read_type(entry, class_group->ext_ref_count);
  if (class_group->ext_ref_count){
    DEBUGF(PARSE, "%u references", class_group->ext_ref_count);
    read_type_array(entry, class_group, class_group->external_refs, class_group->ext_ref_count);
    read_table(entry, class_group, &class_group->main_table);

    class_group->ref_names = pool_alloc_array(class_group->pool, const char *, class_group->ext_ref_count);
    unsigned i;
    for (i=0;i<class_group->ext_ref_count;i++){
      class_group->ref_names[i]=get_table_string(class_group, &class_group->main_table, class_group->external_refs[i].name_offset);
      DEBUGF(PARSE, "External ref[%u] %s", i, class_group->ref_names[i]);
    }
  }

  static uint16_t expect1[] = {0x10,0x32,0x08};
  read_expecting(entry, expect1, 3);

  read_type_defs(entry, class_group, &class_group->global_types);

  read_type(entry, class_group->type_count);
  read_type(entry, class_group->class_count);
  DEBUGF(PARSE, "%u types & %u classes", class_group->type_count, class_group->class_count);

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
    DEBUGF(PARSE, "Type %u", i);
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
      DEBUGF(PARSE, "Class %u", j);

      struct class_def_private *class_def = pool_alloc_type(class_group->pool, struct class_def_private);
      memset(class_def, sizeof(*class_def), 0);
      (*class_ptr) = &class_def->pub;
      class_ptr = &class_def->pub.next;

      struct pbclass_header *cls_header = class_def->header = &class_group->class_headers[j++];

      uint16_t script_count;
      read_type(entry, script_count);
      struct pbscript_list implemented_scripts[script_count];
      read_type(entry, implemented_scripts);

      if (script_count)
	DEBUGF(PARSE, "List of %u implemented scripts sorted by number", script_count);

      unsigned implemented_count=0;
      unsigned k;
      for (k=0;k<script_count;k++)
	if (implemented_scripts[k].implemented)
	  implemented_count++;

      struct script_implementation *implementations = pool_alloc_array(class_group->pool, struct script_implementation, implemented_count);

      unsigned index=0;
      for (k=0; k<script_count; k++){
	if (!implemented_scripts[k].implemented)
	  continue;

	DEBUGF(PARSE, "Script implementation %u, %u", k, implemented_scripts[k].method_number);

	struct script_implementation *implementation = &implementations[index++];
	implementation->number = implemented_scripts[k].method_number;

	read_type(entry, implementation->code_size);
	read_type(entry, implementation->debugline_count);
	uint16_t ignored;
	read_type(entry, ignored);

	DEBUGF(PARSE, "Pcode len = %u", implementation->code_size);
	implementation->code = read_block(entry, class_group, implementation->code_size);
	DEBUGF(PARSE, "Debug line numbers = %u", implementation->debugline_count);
	read_type_array(entry, class_group, implementation->debug_lines, implementation->debugline_count);

	static uint16_t expect4[] = {16,100,8};
	read_expecting(entry, expect4, 3);

	DEBUGF(PARSE, "Local variables");
	read_type_defs(entry, class_group, &implementation->local_variables);
	DEBUGF(PARSE, "References");
	read_table(entry, class_group, &implementation->references);
      }

      if (cls_header->script_count)
	DEBUGF(PARSE, "Script short headers (sorted by id)");
      // IMHO, used  by the runtime to find methods to execute them
      struct pbscript_short_header *short_headers;
      read_type_array(entry, class_group, short_headers, cls_header->script_count);

      // absolutely no idea what this is;
      DEBUGF(PARSE, "No idea");
      uint32_t ignored_array[cls_header->something_count];
      read_type(entry, ignored_array);

      static uint16_t expect5[] = {16,50,11};
      read_expecting(entry, expect5, 3);
      DEBUGF(PARSE, "Event types?");
      read_type_defs(entry, class_group, &class_def->variables);
      read_expecting(entry, expect5, 3);
      DEBUGF(PARSE, "[Re]defined class instance variables");
      read_type_defs(entry, class_group, &class_def->instance_variables);
      DEBUGF(PARSE, "All initial instance values");
      read_type_array(entry, class_group, class_def->instance_values, cls_header->variable_count);
      DEBUGF(PARSE, "Indirect references");
      read_type_array(entry, class_group, class_def->indirect_refs, cls_header->indirect_count);

      if (cls_header->script_count)
	DEBUGF(PARSE, "Script headers (sorted by number)");
      struct pbscript_header *script_headers;
      read_type_array(entry, class_group, script_headers, cls_header->script_count);

      // Link all the script information we have together
      // I assume there are reasons for these tables to be in these orders
      struct script_def_private *script_definitions = pool_alloc_array(class_group->pool, struct script_def_private, cls_header->script_count);
      struct script_definition **script_ptr = &class_def->pub.scripts;

      index=0;
      for (k=0;k<cls_header->script_count;k++){
	// link ->next pointers
	(*script_ptr) = (struct script_definition *)&script_definitions[k];
	script_ptr = &script_definitions[k].pub.next;

	script_definitions[k].short_header = &short_headers[k];

	// TODO bin search?
	unsigned l=0;
	struct script_def_private *script_def=NULL;
	for (l=0;l<cls_header->script_count;l++){
	  if (script_headers[k].method_id == short_headers[l].method_id){
	    script_def = &script_definitions[l];
	    break;
	  }
	}
	assert(script_def);

	script_def->header = &script_headers[k];
	if (implementations[index].number == short_headers[l].method_number){
	  script_def->body = &implementations[index++];
	}else{
	  script_def->body = NULL;
	  assert(implementations[index].number > short_headers[l].method_number);
	}

	script_def->pub.name = get_table_string(class_group, &class_group->function_name_table, script_headers[k].name_offset);
      }
      (*script_ptr) = NULL;
      assert(index == implemented_count);

      for (k=0;k<cls_header->script_count;k++){
	DEBUGF(PARSE, "XXX Script[%u] %s id %u, num %u, (id %u, num %u), %s", k,
	  script_definitions[k].pub.name,
	  script_definitions[k].header->method_id,
	  script_definitions[k].header->method_number,
	  script_definitions[k].short_header->method_id,
	  script_definitions[k].short_header->method_number,
	  script_definitions[k].body ? "implemented":"");
      }
    }
  }

  // did we see all classes?
  assert(j==class_group->class_count);
  {
    // did we hit the end of the binary data? (YAY!)
    uint8_t ignored;
    assert(lib_entry_read(entry, &ignored, 1)==0);
  }

  return (struct class_group *)class_group;
}

void class_free(struct class_group *class_group){
  struct class_group_private *cls = (struct class_group_private *)class_group;
  pool_release(cls->pool);
}
