#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include "pool_alloc.h"
#include "lib.h"
#include "pb_class_types.h"
#include "debug.h"
#include "class_private.h"

#define read_type(E,S) assert(lib_entry_read(E, (uint8_t *)&S, sizeof S)==sizeof S)

// allocate and read bytes
static void* read_block(struct lib_entry *entry, struct class_group_private *class_group, size_t length){
  if (length==0)
    return NULL;
  uint8_t *raw = pool_alloc(class_group->pool, length, 1);
  assert(lib_entry_read(entry, raw, length)==length);
  return (void*)raw;
}

#define read_array(E,CD,S,C) read_block(E,CD,S*C)
// read bytes into array, based on compiled defined sizes
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

const void *get_table_ptr(struct class_group_private *class_group, struct data_table *table, uint32_t offset)
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

const struct pbtable_info *get_table_info(struct class_group_private *class_group, struct data_table *table, uint32_t offset){
  if (offset & 0x80000000){
    table = &class_group->main_table;
    offset = offset & ~0x80000000;
  }

  if (offset == 0xFFFF)
    return NULL;
  assert(offset < table->data_length);
  // TODO binary search?
  unsigned i;
  for (i=0;i<table->metadata_count;i++)
    if (table->metadata[i].offset == offset)
      return &table->metadata[i];
  return NULL;
}

const char *get_table_string(struct class_group_private *class_group, struct data_table *table, uint32_t offset){
  if (class_group->header.compiler_version<PB10)
    return (const char *)get_table_ptr(class_group, table, offset);
  return pool_dup_u(class_group->pool, (const UChar *)get_table_ptr(class_group, table, offset));
}

const char *quote_escape_string(struct class_group_private *class_group, const char *str){
  if (!str)
    return NULL;
  unsigned single_escape_count=0;
  unsigned double_escape_count=0;
  unsigned ascii_non_print=0;
  unsigned len=0;
  const char *s = str;
  while(*s){
    len++;
    switch(*s){
      case '\"':
	double_escape_count++;
	break;
      case '\'':
	single_escape_count++;
	break;
      case '\b':
      case '\v':
      case '\f':
      case '\t':
      case '\r':
      case '\n':
      case '~':
	single_escape_count++;
	double_escape_count++;
	break;
      default:
	if (*s < 0x1f || *s == 0x7f)
	  ascii_non_print++;
    }
    s++;
  }

  char *ret = pool_alloc(class_group->pool, len+double_escape_count+(ascii_non_print*3)+2+1, 1);
  s = str;
  char *d = ret;
  *d++='"';
  while(*s){
    switch(*s){
      case '\b': *d++='~'; *d++='b'; s++; break;
      case '\f': *d++='~'; *d++='f'; s++; break;
      case '\v': *d++='~'; *d++='v'; s++; break;
      case '\r': *d++='~'; *d++='r'; s++; break;
      case '\n': *d++='~'; *d++='n'; s++; break;
      case '\t': *d++='~'; *d++='t'; s++; break;
      case '\"':
      case '~':
	*d++='~';
      default:
	if (*s < 0x1f || *s == 0x7f)
	  d+=sprintf(d, "h%02x", *s++);
	else
	  *d++=*s++;
    }
  }
  *d++='"';
  *d++=0;
  return ret;
}

static const char *get_indirect_arg_name(struct class_group_private *class_group, struct data_table *table, const struct pbindirect_arg *arg){
  switch(arg->indirect_type){
    case indirect_name:
      return "*name";
    case indirect_args:
      return "*args";
    case indirect_nargs:
      return "*nargs";
    case indirect_value:
      return "*value";
    case indirect_eoseq:
      return "*eoseq";
    case indirect_dims:
      return "*dims";
  }
  return get_table_string(class_group, table, arg->expression_offset);
}

static const char *get_indirect_func(struct class_group_private *class_group, struct data_table *table, const struct pbindirect_func *func){
  if (!func)
    return NULL;
  const char *name = get_table_string(class_group, table, func->name_offset);
  if (!name)
    return NULL;
  const char *args[func->arg_count];
  const struct pbindirect_arg *args_ptr = func->arg_count ? get_table_ptr(class_group, table, func->args_offset) : NULL;
  unsigned i;
  size_t len=strlen(name)+2;
  for (i=0;i<func->arg_count;i++){
    args[i] = get_indirect_arg_name(class_group, table, &args_ptr[i]);
    len += args[i] ? strlen(args[i]) : 0;
    if (i>0)
      len+=2;
  }
  char buff[len+1], *p=buff;
  p=strcat(p, name);
  *p++='(';
  for (i=0;i<func->arg_count;i++){
    if (i>0){
      *p++=',';
      *p++=' ';
    }
    if (args[i])
      p=strcat(p, args[i]);
  }
  *p++=')';
  *p++=0;
  return  pool_dupn(class_group->pool, buff, len);
}

const char *get_table_resource(struct class_group_private *class_group, struct data_table *table, uint32_t offset){
  const void *ptr = get_table_ptr(class_group, table, offset);
  if (!ptr)
    return NULL;

  const struct pbtable_info *info = get_table_info(class_group, table, offset);
  if (!info)
    return NULL;

  switch(info->structure_type){
    case 1:
      return pool_sprintf(class_group->pool, "%d", *(const int*)ptr);
    case 4:
      return pool_sprintf(class_group->pool, "%f", *(const double*)ptr);
    case 5:{
      // decimal
      intmax_t magnitude=0; // probably not big enough, but should work for smaller constants.
      uint8_t sign;
      uint8_t exponent;
      if (class_group->header.compiler_version <= PB10){
	struct pb_old_decimal *dec = (struct pb_old_decimal *)ptr;
	sign = dec->sign;
	exponent = dec->exponent;
	memcpy(&magnitude, dec->magnitude, sizeof magnitude > sizeof dec->magnitude ? sizeof dec->magnitude : sizeof magnitude);
      }else{
	struct pb_decimal *dec = (struct pb_decimal *)ptr;
	sign = dec->sign;
	exponent = dec->exponent;
	memcpy(&magnitude, dec->magnitude, sizeof magnitude > sizeof dec->magnitude ? sizeof dec->magnitude : sizeof magnitude);
      }
      if (sign)
	magnitude = -magnitude;
      char buff[32];
      int chars = snprintf(buff, sizeof buff, "%"PRIdMAX, magnitude);
      if (exponent){
	unsigned i;
	for(i=0;i<exponent;i++)
	  buff[chars -i] = buff[chars -i -1];
	buff[chars - exponent]='.';
	buff[chars+1]=0;
      }
      return pool_dup(class_group->pool, buff);
    }
    case 6: {
      const struct pb_datetime *datetime = ptr;
      // probably enough to distinguish dates and times...
      if (datetime->year == 63636 && datetime->month == 255){
	return pool_sprintf(class_group->pool, "%02d:%02d:%02d.%06d",
	  datetime->hour,
	  datetime->minute,
	  datetime->second,
	  datetime->millisecond);
      }else{
	return pool_sprintf(class_group->pool, "%04d-%02d-%02d",
	  datetime->year + 1900,
	  datetime->month + 1,
	  datetime->day);
      }
    }

    // case 9: sql...

    case 12:{ // property reference
      const struct pbprop_ref *ref = (struct pbprop_ref *)ptr;
      const char *name = get_table_string(class_group, table, ref->name_offset);
      if (name)
	return name;
      else
	return pool_sprintf(class_group->pool, "prop_%u", ref->prop_number);
    }
    case 13:{ // method reference
      const struct pbmethod_ref *ref = (struct pbmethod_ref *)ptr;
      const char *name = get_table_string(class_group, table, ref->name_offset);
      if (name)
	return name;
      else
	return pool_sprintf(class_group->pool, "method_%u", ref->method_number);
    }
    case 16:
      return get_indirect_arg_name(class_group, table, ptr);
    case 17:
      return get_indirect_func(class_group, table, ptr);
    case 18:{
      const struct pbcreate_ref *ref = (struct pbcreate_ref *)ptr;
      const char *name = get_table_string(class_group, table, ref->name_offset);
      if (name)
	return name;
      else
	return get_type_name(class_group, ref->type);
    }
    case 19:{
      const struct pbarray_values *definition = ptr;
      const struct pbarray_dimension *dimensions = ptr+sizeof(*definition);
      const struct pbvalue *values = ptr+sizeof(*definition)+(sizeof(*dimensions) * definition->dimensions);
      unsigned i;
      unsigned count=1;
      for (i=0;i<definition->dimensions;i++)
	count*=dimensions[i].upper - dimensions[i].lower + 1;

      const char *svalues[count];
      size_t len=2;
      for (i=0;i<count;i++){
	svalues[i] = get_value(class_group, table, &values[i]);
	len+=svalues[i] ? strlen(svalues[i]) : 0;
	if (i>0)
	  len+=2;
      }
      char buff[len+1], *p = buff;
      *p++='{';
      for (i=0;i<count;i++){
	if (i>0){
	  *p++=',';
	  *p++=' ';
	}
	if (svalues[i])
	  p = strcpy(p, svalues[i]);
      }
      *p++='}';
      *p++=0;
      return pool_dupn(class_group->pool, buff, len);
    }
    case 23:
      return pool_sprintf(class_group->pool, "%"PRId64, *(uint64_t*)ptr);
  }
  return pool_sprintf(class_group->pool, "%02x_%04x", info->structure_type, offset);
}

static unsigned record_sizes[]={0,2,0,0,8,16,12,12,12,56,2,6,8,8,0,0,8,16,8,24,4,0,2,8};

static void dump_table(FILE *fd, struct class_group_private *class_group, struct data_table *table){
  unsigned offset = 0;
  unsigned entry = 0;
  while(offset < table->data_length){
    if (entry < table->metadata_count && offset == table->metadata[entry].offset){
      assert(table->metadata[entry].structure_type >=1 && table->metadata[entry].structure_type <=23);
      unsigned record_size = record_sizes[table->metadata[entry].structure_type];
      assert(record_size);
      fprintf(fd, "%04x %u * [type_%u]:\n", offset, table->metadata[entry].count, table->metadata[entry].structure_type);
      unsigned i;
      unsigned j;
      for(i=0; i<table->metadata[entry].count; i++){
	fprintf(fd, "   [%u]:",i);
	for (j=0;j<record_size;j++)
	  fprintf(fd, " %02x", table->data[offset++]);
	fprintf(fd, "    [%s]\n", get_table_resource(class_group, table, table->metadata[entry].offset));
      }
      entry++;
      continue;
    }

    unsigned data_len;
    if (class_group->header.compiler_version<PB10){
      const char *str = (const char *)&table->data[offset];
      data_len = strlen(str)+1;
      if (entry < table->metadata_count && offset + data_len > table->metadata[entry].offset){
	// bad string? alignment?
	fprintf(fd, "[Skipped %04x]\n", table->metadata[entry].offset - offset);
	offset = table->metadata[entry].offset;
	continue;
      }
      fprintf(fd, "%04x \"%s\"\n", offset, str);
    }else{
      UErrorCode status = U_ZERO_ERROR;
      int32_t dst_len=0;
      const UChar *src=(const UChar *)&table->data[offset];
      int len = u_strlen(src);
      data_len = (len+1)*2;

      if (entry < table->metadata_count && offset + data_len > table->metadata[entry].offset){
	// bad string? alignment?
	fprintf(fd, "[Skipped %04x]\n", table->metadata[entry].offset - offset);
	offset = table->metadata[entry].offset;
	continue;
      }

      u_strToUTF8(NULL, 0, &dst_len, src, len, &status);
      status = U_ZERO_ERROR;
      char buff[dst_len+1];
      u_strToUTF8(buff, dst_len, NULL, src, len, &status);
      buff[dst_len]=0;
      fprintf(fd, "%04x \"%s\"\n", offset, buff);
    }
    offset+=data_len;
  }
  assert(entry == table->metadata_count);
}

void dump_script_resources(FILE *fd, struct class_group *group, struct script_definition *script){
  struct script_def_private *script_def = (struct script_def_private *)script;
  if (!script_def->body)
    return;
  dump_table(fd, (struct class_group_private*)group, &script_def->body->resources);
}

struct class_def_private *get_class_by_type(struct class_group_private *group, uint16_t type){
  if (type==0 || type == 0xC000)
    return NULL;

  if (type & 0x8000){
    unsigned i;
    for (i=0;i<group->pub.type_count;i++){
      if (group->pub.types[i].type == class_type){
	struct class_def_private *class_def = (struct class_def_private *)group->pub.types[i].class_definition;
	if (class_def->type_header->type == type)
	  return class_def;
      }
    }

    return NULL;
  }

  // system types???
  return NULL;
}

const char *get_type_name(struct class_group_private *class_group, uint16_t type){
  if (type==0 || type == 0xC000)
    return NULL;
  if (type & 0x4000){
    // cheating a bit, look for an external reference to this type
    // covers a lot of basic cases
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
    DEBUGF(PARSE, "Type[%u] %s %s [%08x, %04x, %04x, %04x]", i,
      get_type_name(class_group, type_defs->types[i].value.type),
      type_defs->names[i],
      type_defs->types[i].value.value,
      type_defs->types[i].flags,
      type_defs->types[i].unnamed1,
      type_defs->types[i].value.flags);
  }
}

static const char *access_names[]={NULL,"private","protected","system"};

// shift this?
static const char *get_dimensions_str(struct class_group_private *class_group, unsigned count, const struct pbarray_dimension *dimensions){
  if (!dimensions)
    return NULL;

  if (count==0 || (dimensions[0].lower==0 && dimensions[0].upper==0)){
    // shortcut for auto-bound (likely to be common)
    return "[]";
  }
  unsigned j;
  char buff[256], *dst = buff;
  *dst++='[';
  for (j=0;j<count;j++){
    assert(dimensions[j].lower<=dimensions[j].upper);

    if (dimensions[j].lower==0 && dimensions[j].upper==0)
      break;
    if (j>0){
      *dst++=',';*dst++=' ';
    }

    if (dimensions[j].lower==1){
      dst += sprintf(dst, "%d",dimensions[j].upper);
    }else{
      dst += sprintf(dst, "%d to %d", dimensions[j].lower, dimensions[j].upper);
    }
  }
  *dst++=']';
  *dst++=0;
  return pool_dup(class_group->pool, buff);
}

const char *get_value(struct class_group_private *class_group, struct data_table *table, const struct pbvalue *value){
  uint32_t val = value->value;
  if (value->flags & 0x2000)
    // Array
    return get_table_resource(class_group, table, val);

  // 2 byte value
  if ((value->flags & 0xC000) == 0x0400)
    val = val & 0xFFFF;

  if (val==0)
    return NULL;

  switch(value->type){
    case pbvalue_int:
      return pool_sprintf(class_group->pool, "%d", (int16_t)val);
    case pbvalue_long:
      return pool_sprintf(class_group->pool, "%d", (int32_t)val);
    case pbvalue_real:
      return pool_sprintf(class_group->pool, "%f", *(float*)&val);
    case pbvalue_string:{
      const char *raw = get_table_string(class_group, table, val);
      const char *quoted = quote_escape_string(class_group, raw);
      return quoted;
    }
    case pbvalue_double:
    case pbvalue_dec:
    case pbvalue_date:
    case pbvalue_time:
    case pbvalue_longlong:
      return get_table_resource(class_group, table, val);
    case pbvalue_datetime:{
      const struct pb_datetime *datetime = get_table_ptr(class_group, table, val);
      return pool_sprintf(class_group->pool, "datetime(%04d-%02d-%02d, %02d:%02d:%02d.%06d)",
	datetime->year + 1900,
	datetime->month + 1,
	datetime->day,
	datetime->hour,
	datetime->minute,
	datetime->second,
	datetime->millisecond);
    }
    case pbvalue_boolean:
      return val ? "true" : "false";
    case pbvalue_any:
    case pbvalue_blob:
    case pbvalue_objhandle:
    case pbvalue_placeholder:
      return NULL;
    case pbvalue_cursor:
    case pbvalue_procedure:
      return NULL; // TODO
    case pbvalue_char:
      // TODO
    case pbvalue_byte:
    case pbvalue_uint:
      return pool_sprintf(class_group->pool, "%u", (uint16_t)val);
    case pbvalue_ulong:
      return pool_sprintf(class_group->pool, "%u", (uint32_t)val);
  }
  return NULL;
}

static void init_values(struct class_group_private *class_group, struct data_table *table, struct variable_def_private *variable){
  variable->pub.initial_values = NULL;
  variable->pub.value_count = 0;

  if (variable->pub.indirect){
    const struct pbindirect_func *funcs = get_table_ptr(class_group, table, variable->type->value.value);
    if (!funcs)
      return;
    const struct pbtable_info *info = get_table_info(class_group, table, variable->type->value.value);
    if (!info)
      return;

    variable->pub.value_count = info->count;
    variable->pub.initial_values = pool_alloc_array(class_group->pool, const char *, info->count+1);
    unsigned i;
    for (i=0;i<info->count;i++)
      variable->pub.initial_values[i] = get_indirect_func(class_group, table, &funcs[i]);
    variable->pub.initial_values[info->count] = NULL;
    return;
  }

  if (variable->type->value.flags & 0x2000){
    const void *ptr = get_table_ptr(class_group, table, variable->type->value.value);
    if (!ptr)
      return;
    const struct pbarray_values *definition = ptr;
    if (definition->dimensions==0)
      return;
    const struct pbarray_dimension *dimensions = ptr+sizeof(*definition);
    const struct pbvalue *values = ptr+sizeof(*definition)+(sizeof(*dimensions) * definition->dimensions);
    unsigned i;
    unsigned count=1;
    for (i=0;i<definition->dimensions;i++)
      count*=dimensions[i].upper - dimensions[i].lower + 1;
    if (count==0)
      return;
    variable->pub.value_count = count;
    variable->pub.initial_values = pool_alloc_array(class_group->pool, const char *, count+1);
    for (i=0;i<count;i++)
      variable->pub.initial_values[i] = get_value(class_group, table, &values[i]);
    variable->pub.initial_values[count] = NULL;
    return;
  }

  const char *value = get_value(class_group, table, &variable->type->value);
  if (!value)
    return;

  variable->pub.value_count = 1;
  variable->pub.initial_values = pool_alloc_array(class_group->pool, const char *, 2);
  variable->pub.initial_values[0] = value;
  variable->pub.initial_values[1] = NULL;
}

// not all type lists are variables
static struct variable_definition** type_defs_to_variables(struct class_group_private *class_group, struct type_defs *type_defs){
  if (type_defs->count==0)
    return NULL;

  struct variable_definition **pointers = pool_alloc_array(class_group->pool, struct variable_definition*, type_defs->count+1);
  struct variable_def_private *variables = pool_alloc_array(class_group->pool, struct variable_def_private, type_defs->count);
  unsigned i;
  for (i=0;i<type_defs->count;i++){
    pointers[i] = &variables[i].pub;
    const struct pbtype_def *type = variables[i].type = &type_defs->types[i];
    variables[i].pub.name = type_defs->names[i];
    variables[i].pub.type = get_type_name(class_group, type->value.type);
    variables[i].pub.read_access = access_names[(type->flags >> 4)&3];
    variables[i].pub.write_access = access_names[(type->flags >> 6)&3];
    variables[i].pub.user_defined = (type->value.flags & 0x200)?1:0;
    variables[i].pub.constant = (type->flags & 0x04)?1:0;
    variables[i].pub.indirect = (type->flags & 0x02)?1:0;

    const int32_t *ptr = get_table_ptr(class_group, &type_defs->table, type->array_dimensions);
    if (ptr){
      variables[i].dimension_count = (unsigned)(ptr[0] & 0x3FFF);
      variables[i].dimensions = (const struct pbarray_dimension *)&ptr[1];
      variables[i].pub.dimensions = get_dimensions_str(class_group, variables[i].dimension_count, variables[i].dimensions);
    }else{
      variables[i].dimension_count = 0;
      variables[i].dimensions = NULL;
      variables[i].pub.dimensions = NULL;
    }

    init_values(class_group, &type_defs->table, &variables[i]);
  }
  pointers[type_defs->count] = NULL;
  return pointers;
}

static void build_arg_list(struct class_group_private *class_group, struct script_def_private *script_def){
  script_def->pub.signature = get_table_string(class_group, &class_group->arguments_table, script_def->header->signature_offset);
  script_def->arguments = (struct pbarg_def*)get_table_ptr(class_group, &class_group->arguments_table, script_def->header->arguments_offset);
  script_def->argument_info = get_table_info(class_group, &class_group->arguments_table, script_def->header->arguments_offset);

  if (!script_def->argument_info){
    script_def->pub.argument_count=0;
    return;
  }

  unsigned count = script_def->pub.argument_count = script_def->argument_info->count;
  struct argument_definition **pointers = pool_alloc_array(class_group->pool, struct argument_definition *, count+1);
  struct arg_def_private *args = pool_alloc_array(class_group->pool, struct arg_def_private, count);
  memset(args, 0, sizeof(struct arg_def_private)*count);

  script_def->pub.arguments = pointers;
  unsigned i;
  for (i=0;i<count;i++){
    pointers[i] = &args[i].pub;

    switch(script_def->arguments[i].flags & 0x0E){
      case 0x02:
	args[i].pub.access = "ref";
	break;
      case 0x04:
	// ...
	args[i].pub.type = "...";
	// at the end of the arg list, by definition
	return;
      case 0x06:
	args[i].pub.access = "readonly";
	break;
    }

    args[i].pub.name = get_table_string(class_group, &class_group->function_name_table, script_def->arguments[i].name_offset);
    args[i].pub.type = get_type_name(class_group, script_def->arguments[i].type);

    const int32_t *ptr = get_table_ptr(class_group, &class_group->function_name_table, script_def->arguments[i].array_dimensions);
    if (ptr){
      args[i].dimension_count = (unsigned)(ptr[0] & 0x3FFF);
      args[i].dimensions = (const struct pbarray_dimension *)&ptr[1];
      args[i].pub.dimensions = get_dimensions_str(class_group, args[i].dimension_count, args[i].dimensions);
    }else{
      args[i].dimension_count = 0;
      args[i].dimensions = NULL;
      args[i].pub.dimensions = NULL;
    }
  }
  pointers[count] = NULL;
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
  memset(class_group, 0, sizeof(*class_group));

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

  uint16_t type_count;
  read_type(entry, type_count);
  class_group->pub.type_count = type_count;

  read_type(entry, class_group->class_count);
  DEBUGF(PARSE, "%u types & %u classes", type_count, class_group->class_count);

  read_table(entry, class_group, &class_group->function_name_table);
  read_table(entry, class_group, &class_group->arguments_table);

  static uint16_t expect2[] = {0x0a,0x78,0x11};
  read_expecting(entry, expect2, 3);

  read_type_defs(entry, class_group, &class_group->type_list);

  class_group->pub.global_variable_count = class_group->global_types.count;
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

  read_type_array(entry, class_group, class_group->type_headers, type_count);

  struct pbclass_header *class_headers;
  read_type_array(entry, class_group, class_headers, class_group->class_count);

  // now the hard(-ish) part....
  unsigned j=0;
  class_group->pub.types = pool_alloc_array(class_group->pool, struct type_definition, type_count);

  for (i=0;i<type_count;i++){
    class_group->pub.types[i].name = get_type_name(class_group, class_group->type_headers[i].type);
    if ((class_group->type_headers[i].flags & 0xFF) == 3){
      // essentially in pbvmXX.dll(_typedef.grp) only
      class_group->pub.types[i].type = enum_type;
      DEBUGF(PARSE, "Enum %u = %s", i, class_group->pub.types[i].name);
      unsigned count = class_group->type_headers[i].enum_count;

      // not bothering to keep the raw value list around yet
      struct pbenum_value values[count];
      read_type(entry, values);
      size_t size = sizeof(struct enumeration) + count * sizeof(struct enum_value);

      struct enumeration *enumeration = class_group->pub.types[i].enum_definition = pool_alloc(class_group->pool, size,
	alignment_of(struct enumeration));

      memset(enumeration, 0, size);

      enumeration->value_count = count;

      unsigned k;
      for (k=0;k<count;k++){
	enumeration->values[k].name = get_table_string(class_group, &class_group->main_table, values[k].name_offset);
	enumeration->values[k].value = values[k].value;
	DEBUGF(PARSE, " - %s! = %u [%04x]", enumeration->values[k].name, values[k].value, values[k].unnamed);
      }

    }else if(class_group->type_headers[i].flags == 0x85){
      class_group->pub.types[i].type = initsrc;
      class_group->pub.types[i].class_definition = NULL;
      DEBUGF(PARSE, "Type initsrc %u = %s", i, class_group->pub.types[i].name);
    }else if(class_group->type_headers[i].flags == 0x89){
      class_group->pub.types[i].type = sharedsrc;
      class_group->pub.types[i].class_definition = NULL;
      DEBUGF(PARSE, "Type sharedsrc %u = %s", i, class_group->pub.types[i].name);
    }else if(class_group->type_headers[i].flags == 0x0B){
      class_group->pub.types[i].type = globalsrc;
      class_group->pub.types[i].class_definition = NULL;
      DEBUGF(PARSE, "Type globalsrc %u = %s", i, class_group->pub.types[i].name);
    }else{
      // class

      struct class_def_private *class_def = pool_alloc_type(class_group->pool, struct class_def_private);
      memset(class_def, 0, sizeof(*class_def));

      class_group->pub.types[i].type = class_type;
      class_group->pub.types[i].class_definition = (struct class_definition *)class_def;

      class_def->type_header = &class_group->type_headers[i];
      const struct pbclass_header *cls_header = class_def->header = &class_headers[j++];

      class_def->pub.ancestor = get_type_name(class_group, cls_header->ancestor_type);
      class_def->pub.parent = get_type_name(class_group, cls_header->parent_type);
      class_def->pub.autoinstantiate = class_def->type_header->flags & 0x0100?1:0;

      DEBUGF(PARSE, "Class %u = %s (flags %04x, type %04x, ancestor %s, parent %s, %04x, %04x, %04x, %04x, %04x)", i,
	class_group->pub.types[i].name,
	class_group->type_headers[i].flags,
	class_group->type_headers[i].type,
	get_type_name(class_group, cls_header->ancestor_type),
	get_type_name(class_group, cls_header->parent_type),
	cls_header->unnamed1,
	cls_header->unnamed2,
	cls_header->unnamed3,
	cls_header->unnamed4,
	cls_header->unnamed5);

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
	read_table(entry, class_group, &implementation->resources);
      }

      if (cls_header->script_count)
	DEBUGF(PARSE, "Script short headers (sorted by id)");
      // IMHO, used  by the runtime to find methods to execute them
      struct pbscript_short_header *short_headers;
      read_type_array(entry, class_group, short_headers, cls_header->script_count);

      // absolutely no idea what this is;
      if (cls_header->something_count)
	DEBUGF(PARSE, "No idea");
      uint32_t ignored_array[cls_header->something_count];
      read_type(entry, ignored_array);

      static uint16_t expect5[] = {16,50,11};
      read_expecting(entry, expect5, 3);

      // if imports flags &2, value is index in external refs, otherwise value is a counter & the method is in this group
      read_type_defs(entry, class_group, &class_def->imports);
      debug_type_names("method import table", class_group, &class_def->imports);

      read_expecting(entry, expect5, 3);
      read_type_defs(entry, class_group, &class_def->instance_variables);
      class_def->pub.instance_variable_count = class_def->instance_variables.count;
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
      class_def->pub.script_count = cls_header->script_count;
      class_def->pub.scripts = pool_alloc_array(class_group->pool, struct script_definition *, cls_header->script_count+1);

      struct script_def_private *script_definitions = pool_alloc_array(class_group->pool, struct script_def_private, cls_header->script_count);

      index=0;
      for (k=0;k<cls_header->script_count;k++){
	class_def->pub.scripts[k]=&script_definitions[k].pub;

	script_definitions[k].short_header = &short_headers[k];

	// TODO faster with a binary search, but is it worth the hastle?
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
	if (index < implemented_count && implementations[index].number == short_headers[l].method_number){
	  script_def->body = &implementations[index++];
	  script_def->pub.implemented = 1;
	  script_def->pub.local_variable_count = script_def->body->local_variables.count;
	  script_def->pub.local_variables = type_defs_to_variables(class_group, &script_def->body->local_variables);
	}else{
	  script_def->pub.implemented = 0;
	  script_def->pub.local_variable_count = 0;
	  script_def->body = NULL;
	  script_def->pub.local_variables = NULL;
	}

	script_def->pub.name = get_table_string(class_group, &class_group->function_name_table, script_headers[k].name_offset);
	if (script_headers[k].flags & 0x0600)
	  script_def->pub.library = get_table_string(class_group, &class_group->function_name_table, script_headers[k].library_offset);
	script_def->pub.external_name = get_table_string(class_group, &class_group->function_name_table, script_headers[k].alias_offset);
	script_def->pub.return_type = get_type_name(class_group, script_headers[k].return_type);
	script_def->pub.access = access_names[(script_headers[k].flags>>12)&3];
	script_def->pub.event = (script_headers[k].flags & 0x0100)?1:0;
	script_def->pub.hidden = (script_headers[k].more_flags & 1)?1:0;
	script_def->pub.system = (script_headers[k].flags & 0x0200)?1:0;
	script_def->pub.rpc = (script_headers[k].flags & 0x0800)?1:0;
	script_def->pub.in_ancestor = 0; // not sure how to work this out yet....

	script_def->pub.throws_count = script_headers[k].throws_count;
	script_def->pub.throws = pool_alloc_array(class_group->pool, const char *, script_def->pub.throws_count+1);
	script_def->throw_types = get_table_ptr(class_group, &class_group->function_name_table, script_headers[k].throws_offset);

	for (l=0;l<script_def->pub.throws_count;l++)
	  script_def->pub.throws[l] = get_type_name(class_group, script_def->throw_types[l]);
	script_def->pub.throws[script_def->pub.throws_count]=NULL;

	build_arg_list(class_group, script_def);
      }
      assert(index == implemented_count);
      class_def->pub.scripts[cls_header->script_count]=NULL;

      for (k=0;k<cls_header->script_count;k++){
	DEBUGF(PARSE, "Script[%u] %u, %u, %04x, %04x, %04x, %04x, %04x, %04x, %s %s %s %s %s %s, %s", k,
	  script_definitions[k].short_header->method_id,
	  script_definitions[k].short_header->method_number,
	  script_definitions[k].header->unnamed1,
	  script_definitions[k].header->unnamed2,
	  script_definitions[k].header->unnamed3,
	  script_definitions[k].header->unnamed4,
	  script_definitions[k].header->unnamed5,
	  script_definitions[k].header->flags,
	  script_definitions[k].pub.event ? "event" : "function",
	  script_definitions[k].pub.in_ancestor ? "override" : "new",
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
