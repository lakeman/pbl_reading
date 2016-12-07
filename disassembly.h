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
  struct instruction *next;
  struct pcode_def *definition;
  const uint16_t *args;
  unsigned stack_count;
  struct instruction **stack;
  uint8_t begin:1;
  uint8_t end:1;
};

struct statement{
  struct statement *next;
  struct instruction *start;
  struct instruction *end;
};

// define an enum with unique instruction id's
#define DEFINE_OP(NAME,ARGS,STACK_KIND,STACK_ARG) NAME##_##ARGS,
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
};

struct disassembly{
  struct pool *pool;
  struct instruction *instructions;
  struct statement *statements;
};

extern struct pcode_def *PB120_opcodes[];

// this should probably be in a different header....
struct disassembly *disassemble(struct script_definition *script);
void dump_pcode(FILE *fd, struct disassembly *disassembly);
void dump_statements(FILE *fd, struct disassembly *disassembly);
void disassembly_free(struct disassembly *disassembly);

#endif
