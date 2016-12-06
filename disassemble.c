#include <assert.h>
#include <string.h>
#include "class_private.h"
#include "disassembly.h"
#include "debug.h"
#include "pool_alloc.h"

struct disassembly *disassemble(struct script_definition *script){
  struct script_def_private *script_def = (struct script_def_private *)script;

  if (!script_def || !script_def->body || !script_def->body->code)
    return NULL;

  struct pool *pool = pool_create();
  struct disassembly *disassembly = pool_alloc_type(pool, struct disassembly);
  memset(disassembly, sizeof(struct disassembly), 0);

  disassembly->pool = pool;

  unsigned offset=0;
#define MAX_STACK 128
  struct instruction *stack[MAX_STACK];
  unsigned stack_ptr=0;

  struct statement **statement_ptr = &disassembly->statements;
  struct statement *statement = NULL;
  struct instruction **inst_ptr = &disassembly->instructions;

  while(offset < script_def->body->code_size){

    const uint16_t *pc = (const uint16_t*)&script_def->body->code[offset];
    uint16_t opcode = pc[0];

    struct instruction *inst = (*inst_ptr) = pool_alloc_type(pool, struct instruction);
    memset(inst, sizeof(struct instruction), 0);
    inst->offset = offset;
    inst->opcode = opcode;
    inst->definition = PB120_opcodes[opcode];
    inst->args = pc+1;
    inst->next = NULL;
    inst_ptr = &inst->next;

    offset += (1+inst->definition->args)*2;

    init_stack(pool, inst, stack, &stack_ptr);

    if (!statement){
      statement = (*statement_ptr) = pool_alloc_type(pool, struct statement);
      statement->start = inst;
      inst->begin = 1;
      statement->next = NULL;
      statement_ptr = &statement->next;
    }else
      inst->begin = 0;

    if (stack_ptr==0){
      // end of statement
      statement->end = inst;
      inst->end = 1;
      statement = NULL;
    }else
      inst->end = 0;
  }

  if (stack_ptr)
    WARNF("Stack pointer (%u) is not zero!", stack_ptr);
  return disassembly;
}

void dump_pcode(FILE *fd, struct disassembly *disassembly){
  struct instruction *inst = disassembly->instructions;
  while(inst){
    fprintf(fd, "%04x: %04x %s (", inst->offset, inst->opcode, inst->definition->name);

    unsigned arg;
    for(arg = 0; arg < inst->definition->args; arg++){
      if (arg>0)
	fprintf(fd, ", ");
      fprintf(fd, "%04x", inst->args[arg]);
    }
    fprintf(fd, ") [");
    for (arg=0; arg < inst->stack_count; arg++){
      if (arg>0)
	fprintf(fd, ", ");
      if (inst->stack[arg])
	fprintf(fd, "%04x %s", inst->stack[arg]->offset, inst->stack[arg]->definition->name);
      else
	fprintf(fd, "***NULL***");
    }
    fprintf(fd, "]%s%s\n", inst->begin?" BEGIN":"", inst->end?" END":"");
    inst = inst->next;
  }
}

void disassembly_free(struct disassembly *disassembly){
  pool_release(disassembly->pool);
}
