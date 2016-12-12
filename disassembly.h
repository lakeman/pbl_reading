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

// special cases;
enum statement_type{
  expression = 0,
  generated,
  mem_append,
  if_then,
  if_then_endif,
  do_while,
  if_not_then,
  do_until,
  loop_while,
  loop_until,
  loop,
  for_init,
  for_jump,
  for_step,
  for_test,
  jump_goto,
  exception_try,
  exception_catch,
};

struct statement{
  struct statement *next;
  struct statement *prev;
  struct instruction *start;
  struct instruction *end;
  enum statement_type type;
  struct statement *branch;
  unsigned destination_count;
};

enum token_types{
  STACK = 1,
  STACK_CSV,
  STACK_DOT_CSV,
  FUNC_CLASS,
  ARG_INT,
  ARG_CSV,
  ARG_LONG,
  ARG_LONG_HEX,
  RES,
  RES_STRING,
  RES_STRING_CONST,
  TYPE,
  LOCAL,
  GLOBAL,
  SHARED,
  END,
  MAX_TOKEN
};

#define OPERATOR_EQ " = "
#define OPERATOR_NE " <> "
#define OPERATOR_GT " > "
#define OPERATOR_LT " < "
#define OPERATOR_GE " >= "
#define OPERATOR_LE " <= "

#define OPERATOR_ADD " + "
#define OPERATOR_SUB " - "
#define OPERATOR_MULT " * "
#define OPERATOR_DIV " / "
#define OPERATOR_POWER " ^ "
#define OPERATOR_NEGATE "-"

#define OPERATOR_ASSIGNINCR "++"
#define OPERATOR_ASSIGNDECR "--"
#define OPERATOR_ASSIGNADD " += "
#define OPERATOR_ASSIGNSUB " -= "
#define OPERATOR_ASSIGNMULT " *= "

#define BIN_OP(NAME,TYPE) DEFINE_OP(SM_##NAME##_##TYPE, 0, stack_result, 2, "(", STACK, 1, OPERATOR_##NAME, STACK, 0, ")")
#define BIN_OP_ASSIGN(NAME,TYPE) DEFINE_OP(SM_##NAME##ASSIGN_##TYPE, 2, stack_action, 2, STACK, 1, OPERATOR_ASSIGN##NAME, STACK, 0, END)
#define UN_OP(NAME,TYPE) DEFINE_OP(SM_##NAME##_##TYPE, 0, stack_result, 1, "(", OPERATOR_##NAME, STACK, 0, ")")
#define UN_OP_ASSIGN(NAME,TYPE) DEFINE_OP(SM_##NAME##_##TYPE, 2, stack_action, 1, STACK, 0, OPERATOR_ASSIGN##NAME, END)
#define CONVERT(FROM,TO) DEFINE_OP(SM_CNV_##FROM##_TO_##TO, 1, stack_tweak_indirect, 0, STACK, 0)
#define CONVERT2(FROM,TO) DEFINE_OP(SM_CNV_##FROM##_TO_##TO, 2, stack_tweak_indirect, 0, STACK, 0)
#define ASSIGN(TYPE) DEFINE_OP(SM_ASSIGN_##TYPE, 1, stack_action, 2, STACK, 1, " = ", STACK, 0, END)
#define ASSIGN2(TYPE) DEFINE_OP(SM_ASSIGN_##TYPE, 2, stack_action, 2, STACK, 1, " = ", STACK, 0, END)
#define CMP(NAME,TYPE) DEFINE_OP(SM_##NAME##_##TYPE, 0, stack_result, 2, "(", STACK, 1, OPERATOR_##NAME, STACK, 0, ")")
#define CMP2(NAME,TYPE) DEFINE_OP(SM_##NAME##_##TYPE, 2, stack_result, 2, "(", STACK, 1, OPERATOR_##NAME, STACK, 0, ")")
#define METHOD(NAME,FUNC,ARGS,STACK) DEFINE_OP(SM_##NAME, ARGS, stack_result, STACK, #FUNC "(", STACK_CSV, ")")

// define an enum with unique instruction id's
#define DEFINE_OP(NAME,ARGS, ...) NAME##_##ARGS,
enum pcodeid{
#include "opcodes.inc"
MAX_ID
};
#undef DEFINE_OP

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

extern struct pcode_def *PB120_opcodes[];

// this should probably be in a different header....
struct disassembly *disassemble(struct class_group *group, struct class_definition *class_def, struct script_definition *script);
void dump_pcode(FILE *fd, struct disassembly *disassembly);
void dump_statements(FILE *fd, struct disassembly *disassembly);
void disassembly_free(struct disassembly *disassembly);

#endif
