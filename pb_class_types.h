#ifndef pb_class_types_header
#define pb_class_types_header

#include <stdint.h>

#pragma pack(push,1)

enum pb_versions{
// Powerbuilder
  PB3 = 17,
  PB4 = 21,
  PB5 = 79,
  PB6 = 114,
  PB7 = 146,
  PB8 = 166,
  PB9 = 193,
  PB10 = 238,
  PB10_5 = 283,
  PB11 = 316,
  PB11_5 = 321,
  PB12 = 325,
  PB12_5 = 333,
//PocketBuilder
  PK2 = 175,
  PK2_5 = 188
};

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
  pbvalue_byte
};

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
  uint16_t structure_type;
  uint16_t count;
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

#pragma pack(pop)

#endif
