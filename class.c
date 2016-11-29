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

struct variable_def_private{
  struct variable_definition pub;
  const int32_t *dimensions;
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
  struct pbtype_header *type_header;
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
  struct pbtype_header *type_headers;
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

static const void *get_table_ptr(struct class_group_private *class_group, struct data_table *table, uint32_t offset)
{
  if (offset & 0x80000000){
    table = &class_group->main_table;
    offset = offset & ~0x80000000;
  }

  if (offset == 0xFFFF)
    return NULL;

  assert(offset < table->data_length);
  return &table->data[offset];
}

static const char *get_table_string(struct class_group_private *class_group, struct data_table *table, uint32_t offset){
  if (class_group->header.compiler_version<PB10)
    return (const char *)get_table_ptr(class_group, table, offset);
  return pool_dup_u(class_group->pool, (const UChar *)get_table_ptr(class_group, table, offset));
}

static const char *get_type_name(struct class_group_private *class_group, uint16_t type){
  if (type==0 || type == 0xC000)
    return "[VOID]";
  if (type & 0x4000){
    // cheating a bit, look for the first external reference to this type
    unsigned i;
    for (i=0;i<class_group->ext_ref_count;i++)
      if (type == class_group->external_refs[i].system_type
      && type == class_group->external_refs[i].type
      && class_group->external_refs[i].unnamed1 == 0)
	return class_group->ref_names[i];
    return "TODO_SYS_TYPE";
  }
  if (type & 0x8000){
    // the main type list *MUST* already be parsed
    assert((type & ~0x8000)<class_group->type_list.count);
    return class_group->type_list.names[(type & ~0x8000)];
  }
  switch(type){
    case pbvalue_int:		return "int";
    case pbvalue_long:		return "long";
    case pbvalue_real:		return "real";
    case pbvalue_double:	return "double";
    case pbvalue_dec:		return "dec";
    case pbvalue_string:	return "string";
    case pbvalue_boolean:	return "boolean";
    case pbvalue_any:		return "any";
    case pbvalue_uint:		return "uint";
    case pbvalue_ulong:		return "ulong";
    case pbvalue_blob:		return "blob";
    case pbvalue_date:		return "date";
    case pbvalue_time:		return "time";
    case pbvalue_datetime:	return "datetime";
    case pbvalue_cursor:	return "cursor";
    case pbvalue_procedure:	return "procedure";
    case pbvalue_char:		return "char";
    case pbvalue_objhandle:	return "objhandle";
    case pbvalue_longlong:	return "longlong";
    case pbvalue_byte:		return "byte";
  }
  return "[UNKNOWN]";
}

static void read_type_defs(struct lib_entry *entry, struct class_group_private *class_group, struct type_defs *type_defs){
  read_table(entry, class_group, &type_defs->table);
  uint16_t size;
  read_type(entry, size);
  type_defs->count = size / sizeof(struct pbtype_def);
  read_type_array(entry, class_group, type_defs->types, type_defs->count);
  type_defs->names = pool_alloc_array(class_group->pool, const char *, type_defs->count);
  unsigned i;
  for (i=0;i<type_defs->count;i++)
    type_defs->names[i]=get_table_string(class_group, &type_defs->table, type_defs->types[i].name_offset);
}

static void debug_type_names(const char *heading, struct class_group_private *class_group, struct type_defs *type_defs){
  if (type_defs->count==0 || !DEBUG_PARSE)
    return;
  unsigned i;
  DEBUGF(PARSE, "Type list %s", heading);
  for (i=0;i<type_defs->count;i++){
    DEBUGF(PARSE, "Type[%u] %s %s [%04x, %04x]", i,
      get_type_name(class_group, type_defs->types[i].value.type),
      type_defs->names[i],
      type_defs->types[i].flags,
      type_defs->types[i].value.flags);
  }
}

const char *access_names[]={"","private","protected","system"};

// not all type lists are variables
static struct variable_definition* type_defs_to_variables(struct class_group_private *class_group, struct type_defs *type_defs){
  if (type_defs->count==0)
    return NULL;

  struct variable_def_private *variables = pool_alloc_array(class_group->pool, struct variable_def_private, type_defs->count);
  unsigned i;
  for (i=0;i<type_defs->count;i++){
    variables[i].pub.next = (i+1<type_defs->count)?&variables[i+1].pub : NULL;
    variables[i].pub.name = type_defs->names[i];
    variables[i].pub.type = get_type_name(class_group, type_defs->types[i].value.type);
    variables[i].pub.read_access = access_names[(type_defs->types[i].flags >> 4)&3];
    variables[i].pub.write_access = access_names[(type_defs->types[i].flags >> 6)&3];

    const int32_t *ptr = variables[i].dimensions = get_table_ptr(class_group, &type_defs->table, type_defs->types[i].array_dimensions);
    if (ptr){
      unsigned count = (unsigned)(*ptr++ & 0x3FFF);
      if (count==0 || (ptr[0]==0 && ptr[1]==0)){
	// shortcut for auto-bound (likely to be common)
	variables[i].pub.dimensions = "[]";
      }else{
	unsigned j;
	char buff[256], *dst = buff;
	*dst++='[';
	for (j=0;j<count;j++){
	  int32_t lower = *ptr++;
	  int32_t upper = *ptr++;
	  assert(lower<=upper);

	  if (lower==0 && upper==0)
	    break;
	  if (j>0){
	    *dst++=',';*dst++=' ';
	  }

	  if (lower==1){
	    dst += sprintf(dst, "%d",upper);
	  }else{
	    dst += sprintf(dst, "%d to %d", lower, upper);
	  }
	}
	*dst++=']';
	*dst++=0;
	variables[i].pub.dimensions = pool_dup(class_group->pool, buff);
      }
    }else{
      variables[i].pub.dimensions = "";
    }
  }
  return &variables->pub;
}

static void read_expecting(struct lib_entry *entry, const uint16_t *expect, unsigned count){
  uint16_t data[count];
  assert(lib_entry_read(entry, (uint8_t*)&data, sizeof(data))==sizeof(data));
  assert(memcmp(expect, data, sizeof(data))==0);
}

struct class_group *class_parse(struct lib_entry *entry){
  unsigned i;
  struct pool *pool = pool_create();
  struct class_group_private *class_group = pool_alloc_type(pool, struct class_group_private);
  memset(class_group, sizeof(*class_group), 0);

  class_group->pool = pool;

  read_type(entry, class_group->header);
  DEBUGF(PARSE, "header, version %04x, system type %04x",
    class_group->header.compiler_version,
    class_group->header.pb_type);
  assert(class_group->header.compiler_version >= PB6);

  read_type(entry, class_group->ext_ref_count);
  if (class_group->ext_ref_count){
    DEBUGF(PARSE, "%u references", class_group->ext_ref_count);
    read_type_array(entry, class_group, class_group->external_refs, class_group->ext_ref_count);
    read_table(entry, class_group, &class_group->main_table);

    class_group->ref_names = pool_alloc_array(class_group->pool, const char *, class_group->ext_ref_count);
    for (i=0;i<class_group->ext_ref_count;i++)
      class_group->ref_names[i]=get_table_string(class_group, &class_group->main_table, class_group->external_refs[i].name_offset);
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

  class_group->pub.global_variables = type_defs_to_variables(class_group, &class_group->global_types);
  debug_type_names("globals", class_group, &class_group->global_types);

  if (class_group->type_list.count>0 && DEBUG_PARSE){
    for (i=0;i<class_group->ext_ref_count;i++){
      DEBUGF(PARSE, "External ref[%u] [%04x, %04x (%s), %04x (%s), %04x] %s", i,
	class_group->external_refs[i].unnamed1,
	class_group->external_refs[i].system_type,
	get_type_name(class_group, class_group->external_refs[i].system_type),
	class_group->external_refs[i].type,
	get_type_name(class_group, class_group->external_refs[i].type),
	class_group->external_refs[i].unnamed2,
	class_group->ref_names[i]);
    }
    DEBUGF(PARSE, "Main type list");
    struct type_defs *type_defs = &class_group->type_list;
    for (i=0;i<type_defs->count;i++){
      unsigned ext_ref = type_defs->types[i].value.value & 0xFFFF;

      DEBUGF(PARSE, "Type[%u] %s %s (%08x %04x %s)", i,
	get_type_name(class_group, type_defs->types[i].value.type),
	type_defs->names[i],
	type_defs->types[i].value.value,
	type_defs->types[i].value.flags,
	class_group->ref_names[ext_ref]
	);

    }
    // TODO other flags? eg link external ref info
  }

  static uint16_t expect3[] = {0x14,0xf0,0x11};
  read_expecting(entry, expect3, 3);

  read_type_defs(entry, class_group, &class_group->enum_values);
  debug_type_names("enum values", class_group, &class_group->enum_values);

  read_type_array(entry, class_group, class_group->type_headers, class_group->type_count);

  struct pbclass_header *class_headers;
  read_type_array(entry, class_group, class_headers, class_group->class_count);

  // now the hard(-ish) part....
  unsigned j=0;
  struct enumeration **enum_ptr = &class_group->pub.enumerations;
  struct class_definition **class_ptr = &class_group->pub.classes;

  for (i=0;i<class_group->type_count;i++){
    if ((class_group->type_headers[i].flags & 0xFF) == 3){
      // essentially in pbvmXX.dll(_typedef.grp) only
      DEBUGF(PARSE, "Enum %u = %s", i, get_type_name(class_group, class_group->type_headers[i].type));
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
      DEBUGF(PARSE, "Type %u = %s", i, get_type_name(class_group, class_group->type_headers[i].type));
    }else{
      // class

      struct class_def_private *class_def = pool_alloc_type(class_group->pool, struct class_def_private);
      memset(class_def, sizeof(*class_def), 0);
      (*class_ptr) = &class_def->pub;
      class_ptr = &class_def->pub.next;

      class_def->type_header = &class_group->type_headers[i];
      struct pbclass_header *cls_header = class_def->header = &class_headers[j++];

      class_def->pub.name = get_type_name(class_group, class_group->type_headers[i].type);
      class_def->pub.ancestor = get_type_name(class_group, cls_header->ancestor_type);
      class_def->pub.parent = get_type_name(class_group, cls_header->parent_type);

      DEBUGF(PARSE, "Class %u = %s (type %04x, ancestor %s, parent %s)", i,
	class_def->pub.name,
	class_group->type_headers[i].type,
	get_type_name(class_group, cls_header->ancestor_type),
	get_type_name(class_group, cls_header->parent_type));

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

	read_type_defs(entry, class_group, &implementation->local_variables);
	debug_type_names("local variables", class_group, &implementation->local_variables);
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
      read_type_defs(entry, class_group, &class_def->variables);
      debug_type_names("method returns?", class_group, &class_def->variables);
      read_expecting(entry, expect5, 3);
      read_type_defs(entry, class_group, &class_def->instance_variables);
      class_def->pub.instance_variables = type_defs_to_variables(class_group, &class_def->instance_variables);
      debug_type_names("instance variables", class_group, &class_def->instance_variables);
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
	  script_def->pub.local_variables = type_defs_to_variables(class_group, &script_def->body->local_variables);
	}else{
	  script_def->body = NULL;
	  script_def->pub.local_variables = NULL;
	  assert(implementations[index].number > short_headers[l].method_number);
	}

	script_def->pub.name = get_table_string(class_group, &class_group->function_name_table, script_headers[k].name_offset);
	script_def->pub.signature = get_table_string(class_group, &class_group->arguments_table, script_headers[k].signature_offset);
	if (script_headers[k].flags & 0x0600)
	  script_def->pub.library = get_table_string(class_group, &class_group->function_name_table, script_headers[k].library_offset);
	script_def->pub.external_name = get_table_string(class_group, &class_group->function_name_table, script_headers[k].alias_offset);
      }
      (*script_ptr) = NULL;
      assert(index == implemented_count);

      for (k=0;k<cls_header->script_count;k++){
	DEBUGF(PARSE, "XXX Script[%u] %s %s %s %s, %s", k,
	  script_definitions[k].pub.name,
	  script_definitions[k].pub.signature,
	  script_definitions[k].pub.library,
	  script_definitions[k].pub.external_name,
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
