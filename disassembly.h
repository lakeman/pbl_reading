#ifndef disassembly_header
#define disassembly_header

#include <stdint.h>
#include <stdio.h>
#include "pb_class_types.h"

struct script_definition;
struct pcode_def;
struct pool;

struct instruction{
  unsigned offset;
  uint16_t opcode;
  unsigned line_number;
  struct pcode_def *definition;
  const uint16_t *args;
  unsigned stack_count;
  struct instruction **stack;
  uint8_t begin:1;
  uint8_t end:1;
};

struct scope{
  struct scope *parent;
  struct statement *start;
  struct statement *indent_start;
  struct statement *indent_end;
  struct statement *end;
  struct statement *break_dest; // end if, catch, exit ...
  struct statement *continue_dest; // else, continue, finally, ...
  const char *begin_label;
  const char *end_label;
};

// special cases;
// TODO bit flags?
enum statement_type{
  expression = 0,
  generated,
  mem_append,
  // unclassified branches
  jump_true,
  jump_false,
  jump_goto,
  // flow control that has been classified;
  if_then,
  do_while,
  do_until,
  loop_while,
  loop_until,
  jump_loop,
  jump_next,
  jump_exit,
  jump_continue,
  jump_else,
  jump_elseif,
  choose_case,
  case_if,
  case_else,
  // for loops must have the following 4 statements on a single line;
  for_init,
  for_jump,
  for_step,
  for_test,
  exception_try,
  exception_catch,
  exception_end_try,
  exception_gosub
};

struct statement{
  struct statement *next;
  struct statement *prev;
  struct instruction *start;
  struct instruction *end;
  struct scope *scope;
  unsigned start_line_number;
  unsigned end_line_number;
  unsigned start_offset;
  unsigned end_offset;
  enum statement_type type;
  struct statement *branch;
  unsigned destination_count;
  unsigned classified_count;
};

enum token_types{
  STACK = 1,
  STACK_CSV,
  STACK_DOT_CSV,
  FUNC_CLASS,
  ARG_BOOL,
  ARG_INT,
  ARG_CSV,
  ARG_LONG,
  ARG_LONG_HEX,
  ARG_ENUM,
  OPERATOR,
  METHOD_FLAGS,
  RES,
  RES_STRING,
  RES_STRING_CONST,
  TYPE,
  LOCAL,
  GLOBAL,
  SHARED,
  EXT,
  END,
  MAX_TOKEN
};

enum operation{
  OP_OTHER,
  OP_EQ,
  OP_NE,
  OP_GT,
  OP_LT,
  OP_GE,
  OP_LE,
  OP_CAT,
  OP_ADD,
  OP_SUB,
  OP_MULT,
  OP_DIV,
  OP_POWER,
  OP_NEGATE,
  OP_AND,
  OP_OR,
  OP_NOT,
  OP_ASSIGN,
  OP_ASSIGNINCR,
  OP_ASSIGNDECR,
  OP_ASSIGNADD,
  OP_ASSIGNSUB,
  OP_ASSIGNMULT,
  OP_CONST,
  OP_CONVERT,
};

// note the gaps, as precedence is decremented when testing RHS of a binary operation
// to detect when left to right rule has been violated. eg a - (b + c)
#define PRECEDENCE_NEGATE 2
#define PRECEDENCE_POWER 4
#define PRECEDENCE_MULT 6
#define PRECEDENCE_DIV 6
#define PRECEDENCE_CAT 8
#define PRECEDENCE_ADD 8
#define PRECEDENCE_SUB 8
#define PRECEDENCE_COMPARE 10
#define PRECEDENCE_NOT 12
#define PRECEDENCE_AND 14
#define PRECEDENCE_OR 16

#define BIN_OP(NAME,TYPE) __DEFINE(SM_##NAME##_##TYPE, 0, stack_result, 2, PRECEDENCE_##NAME, OP_##NAME, STACK, 1, OPERATOR, STACK, 0)
#define BIN_OP_ASSIGN(NAME,TYPE) __DEFINE(SM_##NAME##ASSIGN_##TYPE, 2, stack_action, 2, 0, OP_ASSIGN##NAME, STACK, 1, OPERATOR, STACK, 0, END)
#define UN_OP(NAME,TYPE) __DEFINE(SM_##NAME##_##TYPE, 0, stack_result, 1, PRECEDENCE_##NAME, OP_##NAME, OPERATOR, STACK, 0)
#define UN_OP_ASSIGN(NAME,TYPE) __DEFINE(SM_##NAME##_##TYPE, 2, stack_action, 1, 0, OP_ASSIGN##NAME, STACK, 0, OPERATOR, END)
#define CONVERT(FROM,TO) __DEFINE(SM_CNV_##FROM##_TO_##TO, 1, stack_tweak_indirect, 0, 0, OP_CONVERT, STACK, 0)
#define CONVERT2(FROM,TO) __DEFINE(SM_CNV_##FROM##_TO_##TO, 2, stack_tweak_indirect, 0, 0, OP_CONVERT, STACK, 0)
#define ASSIGN(TYPE) __DEFINE(SM_ASSIGN_##TYPE, 1, stack_action, 2, 0, OP_ASSIGN, STACK, 1, OPERATOR, STACK, 0, END)
#define ASSIGN2(TYPE) __DEFINE(SM_ASSIGN_##TYPE, 2, stack_action, 2, 0, OP_ASSIGN, STACK, 1, OPERATOR, STACK, 0, END)
#define CMP(NAME,TYPE) __DEFINE(SM_##NAME##_##TYPE, 0, stack_result, 2, PRECEDENCE_COMPARE, OP_##NAME, STACK, 1, OPERATOR, STACK, 0)
#define CMP2(NAME,TYPE) __DEFINE(SM_##NAME##_##TYPE, 2, stack_result, 2, PRECEDENCE_COMPARE, OP_##NAME, STACK, 1, OPERATOR, STACK, 0)
#define CONST(TYPE, ARGTYPE) __DEFINE(SM_PUSH_CONST_##TYPE, 1, stack_result, 0, 0, OP_CONST, ARGTYPE, 0)
#define CONST2(TYPE, ARGTYPE) __DEFINE(SM_PUSH_CONST_##TYPE, 2, stack_result, 0, 0, OP_CONST, ARGTYPE, 0)
#define METHOD(NAME,FUNC,ARGS,STACK) __DEFINE(SM_##NAME, ARGS, stack_result, STACK, 0, OP_OTHER, "::" #FUNC "(", STACK_CSV, ")")

#define DEFINE_OP(NAME,ARGS,STACK_KIND,STACK_ARG,PRECEDENCE,...) __DEFINE(NAME,ARGS,STACK_KIND,STACK_ARG,PRECEDENCE,OP_OTHER,##__VA_ARGS__)



// define an enum with unique instruction id's
#define __DEFINE(NAME,ARGS, ...) NAME##_##ARGS,
#define BEFORE(X) 1
#define AFTER(X) 1

enum pcodeid{
#include "opcodes.inc"
MAX_ID
};

#undef BEFORE
#undef AFTER
#undef __DEFINE

// ways that instructions interaction with the stack?
enum stack_kind{
  stack_unknown,
  stack_none,
  stack_result, // operators etc
  stack_result_indirect,
  stack_clone_indirect, // duplicate the LV onto the stack, eg string +=
  stack_action, // assignments, branches etc
  stack_action_indirect,
  stack_tweak_indirect, // type promotion before / after an operator / call
  stack_popn, // preserve the topmost stack item, remove N items below it
  stack_popn_indirect,
  stack_peek_result,
  stack_peek_result_indirect,
  stack_dotcall, // object reference was pushed onto stack first
  stack_classcall, // function reference was pushed onto stack last
  stack_tweak_indirect1, // SM_FREE_REF_PAK_N (sigh)
};

struct pcode_def{
  unsigned id;
  const char *name;
  const char *description;
  unsigned args;
  unsigned precedence;
  enum operation operation;
  enum stack_kind stack_kind;
  unsigned stack_arg;
  const char *tokens[];
};

struct disassembly{
  struct pool *pool;
  struct class_group *group;
  struct class_definition *class_def;
  struct script_definition *script;
  unsigned instruction_count;
  struct instruction **instructions;
  unsigned statement_count;
  struct statement **statements;
};

extern struct pcode_def *PB50_opcodes[];
extern struct pcode_def *PB80_opcodes[];
extern struct pcode_def *PB90_opcodes[];
extern struct pcode_def *PB100_opcodes[];
extern struct pcode_def *PB105_opcodes[];
extern struct pcode_def *PB120_opcodes[];

extern unsigned PB50_maxcode;
extern unsigned PB80_maxcode;
extern unsigned PB90_maxcode;
extern unsigned PB100_maxcode;
extern unsigned PB105_maxcode;
extern unsigned PB120_maxcode;

// this should probably be in a different header....
struct disassembly *disassemble(struct class_group *group, struct class_definition *class_def, struct script_definition *script);
void dump_pcode(FILE *fd, struct disassembly *disassembly);
void dump_statements(FILE *fd, struct disassembly *disassembly);
void dump_raw_pcode(FILE *fd, struct script_definition *script);
void disassembly_free(struct disassembly *disassembly);

#endif
