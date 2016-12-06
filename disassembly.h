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

struct pcode_def{
  unsigned id;
  const char *name;
  const char *description;
  unsigned args;
};

struct disassembly{
  struct pool *pool;
  struct instruction *instructions;
  struct statement *statements;
};

extern struct pcode_def *PB120_opcodes[];
void init_stack(struct pool *pool, struct instruction *i, struct instruction **stack, unsigned *stack_ptr);

// this should probably be in a different header....
struct disassembly *disassemble(struct script_definition *script);
void dump_pcode(FILE *fd, struct disassembly *disassembly);
void disassembly_free(struct disassembly *disassembly);

#endif
