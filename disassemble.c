#include <assert.h>
#include <string.h>
#include "class_private.h"
#include "disassembly.h"
#include "debug.h"
#include "pool_alloc.h"

#define PUSH(X) stack[(*stack_ptr)++]=X
#define POP() (*stack_ptr>0 ? stack[--(*stack_ptr)] : NULL)
#define PEEK(I) (*stack_ptr>(I) ? stack[(*stack_ptr) - (I) -1] : NULL)
#define POKE(I,V) if (*stack_ptr>(I)) stack[(*stack_ptr) - (I) -1] = (V)
// emulate the impact each instruction has on the stack
// to discover how each input value is calculated
static void init_stack(struct pool *pool, struct instruction *inst, struct instruction **stack, unsigned *stack_ptr){
  inst->stack_count = 0;
  inst->stack = NULL;

  unsigned i;
  unsigned stack_arg = inst->definition->stack_arg;
  struct instruction *push_me = inst;

  switch(inst->definition->stack_kind){
    case stack_unknown:
    case stack_none:
      return;

    // preserve the current top of the stack, discard the next stack_arg items.
    case stack_popn:
      push_me = POP();
      goto stack_result;

    case stack_popn_indirect:
      push_me = POP();
      // fallthrough

    // pop stack_arg arguments & push the result of this instruction onto the stack
    case stack_result_indirect:
stack_result_indirect:
      stack_arg = inst->args[stack_arg];
      // fallthrough
    case stack_result:
stack_result:
      inst->stack_count = stack_arg;
      if (stack_arg){
	inst->stack = pool_alloc_array(pool, struct instruction*, stack_arg);
	for (i=0;i<stack_arg;i++)
	  inst->stack[i]=POP();
      }
      if (push_me)
	PUSH(push_me);
      break;

    // pop stack_arg arguments with no result
    case stack_action_indirect:
      push_me = NULL;
      goto stack_result_indirect;
    case stack_action:
      push_me = NULL;
      goto stack_result;

    // convert / free the value at this stack offset
    case stack_tweak_indirect:
      stack_arg = inst->args[stack_arg];
      inst->stack = pool_alloc_type(pool, struct instruction*);
      inst->stack_count = 1;
      inst->stack[0] = PEEK(stack_arg -1);
      POKE(stack_arg -1, inst);
      break;
    case stack_tweak_indirect1:
      stack_arg = inst->args[stack_arg]+1;
      inst->stack = pool_alloc_type(pool, struct instruction*);
      inst->stack_count = 1;
      inst->stack[0] = PEEK(stack_arg -1);
      POKE(stack_arg -1, inst);
      break;

    // duplicate the LHS of an expression, to reuse it on the RHS. eg string +=
    case stack_clone_indirect:
      stack_arg = inst->args[stack_arg];
      inst->stack = pool_alloc_type(pool, struct instruction*);
      inst->stack[0] = PEEK(stack_arg -1);
      inst->stack_count = 1;
      PUSH(inst);
      break;

    // Argument values are left on the stack so they may be cleaned up after this instruction
    // Followed by a popn operation to preserve the result
    case stack_peek_result_indirect:
      stack_arg = inst->args[stack_arg];
    case stack_peek_result:
      inst->stack_count = stack_arg;
      if (stack_arg){
	inst->stack = pool_alloc_array(pool, struct instruction*, stack_arg);
	for (i=0;i<stack_arg;i++)
	  inst->stack[i]=PEEK(i);
      }
      PUSH(inst);
      break;

    // an object reference was pushed onto the stack before the argument list
    case stack_dotcall:
      stack_arg = inst->args[stack_arg];
      inst->stack = pool_alloc_array(pool, struct instruction*, stack_arg + 1);
      inst->stack_count = stack_arg+1;
      inst->stack[0] = PEEK(stack_arg);
      for (i=0;i<stack_arg;i++)
	inst->stack[i+1]=PEEK(i);
      PUSH(inst);
      break;

    // class & function information was pushed onto the stack last
    case stack_classcall:
      stack_arg = inst->args[stack_arg];
      inst->stack = pool_alloc_array(pool, struct instruction*, stack_arg + 1);
      inst->stack_count = stack_arg+1;
      inst->stack[0] = POP();
      for (i=0;i<stack_arg;i++)
	inst->stack[i+1]=PEEK(i);
      PUSH(inst);
      break;
  }
}

#undef PUSH
#undef POP
#undef PEEK
#undef POKE

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

    DEBUGF(DISASSEMBLY,"%04x: %04x %s, %u, %u", inst->offset, opcode, inst->definition->name, inst->stack_count, stack_ptr);

    if (!statement){
      statement = (*statement_ptr) = pool_alloc_type(pool, struct statement);
      statement->start = inst;
      inst->begin = 1;
      statement->next = NULL;
      statement_ptr = &statement->next;
    }else
      inst->begin = 0;

    statement->end = inst;

    if (stack_ptr==0){
      // end of statement
      inst->end = 1;
      statement = NULL;
    }else
      inst->end = 0;
  }

  if (stack_ptr)
    WARNF("Stack pointer (%u) is not zero at the end of %s!", stack_ptr, script->name);
  return disassembly;
}

void dump_pcode(FILE *fd, struct disassembly *disassembly){
  struct instruction *inst = disassembly->instructions;
  while(inst){
    fprintf(fd, "%04x: %04x ", inst->offset, inst->opcode);
    unsigned arg;
    for(arg = 0; arg < inst->definition->args; arg++)
      fprintf(fd, "%04x ", inst->args[arg]);
    for(;arg < 6; arg++)
      fprintf(fd, "     ");

    fprintf(fd, "%s (", inst->definition->name);
    for (arg=0; arg < inst->stack_count; arg++){
      if (arg>0)
	fprintf(fd, ", ");
      if (inst->stack && inst->stack[arg])
	fprintf(fd, "%04x %s ", inst->stack[arg]->offset, inst->stack[arg]->definition->name);
      else
	fprintf(fd, "NULL");
    }
    fprintf(fd, ")\n");

    inst = inst->next;
  }
}

static void dump_instruction(FILE *fd, struct instruction *inst){
  if (!inst){
    fprintf(fd, "***NULL***");
    return;
  }
  fprintf(fd, "%s(", inst->definition->name);
  unsigned arg;
  for (arg=0; arg < inst->stack_count; arg++){
    if (arg>0)
      fprintf(fd, ", ");
    dump_instruction(fd, inst->stack[arg]);
  }
  fprintf(fd, ")");
}

void dump_statements(FILE *fd, struct disassembly *disassembly){
  struct statement *statement = disassembly->statements;
  while(statement){
    dump_instruction(fd, statement->end);
    fprintf(fd, ";\n");
    statement = statement->next;
  }
}

void disassembly_free(struct disassembly *disassembly){
  pool_release(disassembly->pool);
}
