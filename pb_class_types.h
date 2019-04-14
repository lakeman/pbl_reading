#ifndef pb_class_types_header
#define pb_class_types_header

#include <stdint.h>

#pragma pack(push,1)

// Powerbuilder
#define PB30 17
#define PB40 21
#define PB50 79
#define PB60 114
#define PB70 146
#define PB80 166
#define PB90 193
#define PB100 238
#define PB105 283
#define PB110 316
#define PB115 321
#define PB120 325
#define PB125 333
#define PB150 334
#define PB170 335
//PocketBuilder
#define PK20 175
#define PK25 188

enum pbtype{
  pbvalue_notype = 0,
  pbvalue_int,
  pbvalue_long,
  pbvalue_real,
  pbvalue_double,
  pbvalue_dec,
  pbvalue_string,
  pbvalue_boolean,
  pbvalue_any,
  pbvalue_uint,
  pbvalue_ulong,
  pbvalue_blob,
  pbvalue_date,
  pbvalue_time,
  pbvalue_datetime,
  pbvalue_cursor,	// undocumented
  pbvalue_procedure,	// undocumented
  pbvalue_placeholder,  // undocumented && unknown
  pbvalue_char,
  pbvalue_objhandle,	// undocumented
  pbvalue_longlong,
  pbvalue_byte,
  pbvalue_longptr
};

struct pbfile_header{
  uint16_t compiler_version;
  uint16_t format_version;
  uint16_t pb_type;
  uint16_t unnamed1;
  uint16_t unnamed2;
  uint16_t unnamed3;
  uint32_t timestamp1;
  uint32_t timestamp2;
  uint16_t compilation_state;
  uint16_t unnamed4;
};

struct pbtable_info{
  uint32_t offset;
// TODO define structure type enum?
  uint16_t structure_type;
  uint16_t count;
};

struct pbvalue{
  uint32_t value;
  uint16_t flags;
  uint16_t type;
};

struct pbtype_def{
  uint16_t flags;
  uint16_t unnamed1;
  uint32_t array_dimensions;
  uint32_t name_offset;
  struct pbvalue value;
};

struct pbtype_header{
  uint16_t flags;
  uint16_t type;
  uint16_t enum_count;
  uint16_t type2;
  // If version >= PB6 (114)
  uint32_t unnamed1;
  uint32_t unnamed2;
  // end if
};

struct pbclass_header{
  uint16_t ancestor_type;
  uint16_t parent_type;
  uint16_t script_count;
  uint16_t unnamed5;
  uint16_t script_something_count;
  uint16_t unnamed1;
  uint16_t unnamed2;
  uint16_t unnamed3;
  uint16_t method_count;
  uint16_t function_count;
  uint16_t event_count;
  uint16_t something_count;
  uint16_t unnamed4;
  uint16_t indirect_count;
  uint16_t variable_count;
  uint16_t ancestor_variable_count;
};

struct pbenum_value{
  uint32_t name_offset;
  uint16_t value;
  uint16_t unnamed;
};

struct pbscript_list{
  uint16_t implemented;
  uint16_t method_number;
};

struct pbscript_short_header{
  uint16_t method_id;
  uint16_t method_number;
  uint16_t type; // type it is implemented in?
};

struct pbscript_header{
  uint32_t name_offset;
  // If version >= PB6 (114)
  uint32_t signature_offset;
  // end if
  uint32_t arguments_offset;
  uint32_t alias_offset;
  uint32_t library_offset;
  uint16_t method_id;
  uint16_t method_number;
  uint16_t system_function;
  uint16_t unnamed1;
  uint16_t return_type;
  uint16_t flags;
  uint16_t event_id;
  uint8_t more_flags;
  uint8_t throws_count;
  // If version >= PB6 (114)
  uint16_t unnamed2;
  uint16_t unnamed3;
  uint16_t unnamed4;
  uint16_t unnamed5;
  // end if
  // If version >= PB8 (166)
  uint32_t throws_offset;
  // end if
};

struct pbindirect_ref{
  uint32_t name_offset;
  uint32_t arg_list_offset;
  uint16_t arg_count;
  uint16_t unnamed1;
  // If version >= PB6 (114)
  uint32_t unnamed2;
  // end if
};

struct pbdebug_line_num{
  uint16_t line_number;
  uint16_t pcode_offset;
};




// structures that might appear within a data table;
struct pb_datetime{
  uint32_t millisecond;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t day_of_week;
};

struct pbprop_ref{
  uint32_t name_offset;
  uint16_t prop_number;
  uint16_t type;
};

struct pbmethod_ref{
  uint16_t method_number;
  uint16_t type;
  uint32_t name_offset;
};

struct pbcreate_ref{
  uint32_t name_offset;
  uint16_t type;
  uint16_t flags;
};

struct pbext_reference{
  uint32_t name_offset;
  uint16_t unnamed1;
  uint16_t system_type;
  uint16_t type;
  uint16_t unnamed2;
};

struct pbarray_dimension{
  int32_t lower;
  int32_t upper;
};

struct pbarray_values{
  uint16_t unnamed1;
  uint16_t type;
  uint32_t unnamed2;
  uint32_t unnamed3;
  uint16_t unnamed4;
  uint16_t dimensions;
  uint32_t unnamed5;
  uint32_t unnamed6;
  uint32_t unnamed7;
  uint32_t unnamed8;
};

enum indirect_type{
  indirect_name=1,
  indirect_args,
  indirect_nargs,
  indirect_value,
  indirect_eoseq,
  indirect_dims
};

struct pbindirect_arg{
  uint32_t expression_offset;
  uint16_t indirect_type;
  uint16_t unnamed2;
};

struct pbindirect_func{
  uint32_t name_offset;
  uint32_t args_offset;
  uint16_t arg_count;
  uint16_t unnamed1;
  uint32_t unnamed2;
};


struct pbarg_def{
  uint32_t name_offset;
  uint32_t array_dimensions;
  uint16_t type;
  uint16_t flags;
};

struct pb_decimal{
  uint8_t magnitude[14];
  uint8_t sign;
  uint8_t exponent;
};

struct pb_old_decimal{
  uint8_t sign;
  uint8_t exponent;
  uint8_t magnitude[10];
};

enum fetch_direction{
  fetch_next=1,
  fetch_first,
  fetch_prior,
  fetch_last,
};

struct pb_sql{
  uint32_t type;
  uint32_t unnamed1;
  // the sql statement is defined in another pb_sql struct here (what a waste of space...)
  uint32_t related_sql_offset;
  uint32_t cursor_name_offset;
  // list of start, end positions within the sql string, where a constant can be overridden with a bound input parameter
  uint32_t bind_list_offset;
  uint32_t unnamed2;
  // raw sql string, with constant values for bound input parameters
  uint32_t sql_offset;
  uint32_t unnamed3;
  uint32_t unnamed4;
  uint32_t fetch_direction;
  uint32_t input_count;
  uint32_t output_count;
  uint32_t unnamed5;
  uint32_t unnamed6;
};

#pragma pack(pop)

#endif
