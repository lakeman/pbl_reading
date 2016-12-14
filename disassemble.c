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

// a forwards jumpfalse might be for_next, do_while or just if_then
static void classify_if_then(struct disassembly *disassembly, unsigned statement_number)
{
  struct statement *if_test = disassembly->statements[statement_number];
  struct statement *prior = if_test->branch->prev;

  // loops end with a jump back to the start
  if (prior->type != jump_goto)
    return;

  unsigned dest_offset = prior->end->args[0];
  if (dest_offset == if_test->start->offset){
    if_test->type = do_while;
    return;
  }

  /* for var = initial_value to end_value [step step_by]
   * 	[body]
   * next
   *
   * is equivalent to;
   *
   * var = initial_value; goto if_test; step: var++; [OR var += step_by;] if_test: if var <= end_value then
   *   [body]
   * goto step; end if
   */

  if (statement_number >= 3
    && disassembly->statements[statement_number -3]->start->line_number == if_test->end->line_number
    && dest_offset == disassembly->statements[statement_number -1]->start->offset
    && disassembly->statements[statement_number -2]->type == jump_goto
    // more explicitly;
    // && statement_number -3 == SM_ASSIGN_[TYPE]
    // && statement_number -1 == SM_INCR_[TYPE] || SM_ADDASSIGN_[TYPE]
    // && same variable in all cases
    ){
    disassembly->statements[statement_number -3]->type = for_init;
    disassembly->statements[statement_number -2]->type = for_jump;
    disassembly->statements[statement_number -1]->type = for_step;
    if_test->type = for_test;
    return;
  }
}

static void link_destinations(struct disassembly *disassembly){
  unsigned i;
  // find the destination of each jump
  for (i=0;i<disassembly->statement_count;i++){
    struct statement *ptr = disassembly->statements[i];
    switch(ptr->type){
      case exception_catch:
	assert(ptr->end->definition->id == SM_JUMPFALSE_1);
	goto find_dest;

      case if_then:
      case jump_goto:
      case loop_while:
      case loop_until:
      case do_until:
find_dest:
      {
	unsigned dest_offset = ptr->end->args[0];
	unsigned j;
	// TODO binary search?
	for (j=0;j<disassembly->statement_count;j++){
	  if (disassembly->statements[j]->start->offset == dest_offset){
	    ptr->branch = disassembly->statements[j];
	    ptr->branch->destination_count++;
	    break;
	  }
	}
      } break;

      default:
	break;
    }

    if (ptr->type == if_then)
      classify_if_then(disassembly, i);

  }
}

struct disassembly *disassemble(struct class_group *group, struct class_definition *class_def, struct script_definition *script){
  struct script_def_private *script_def = (struct script_def_private *)script;

  if (!script_def || !script_def->body || !script_def->body->code)
    return NULL;

  struct pool *pool = pool_create();
  struct disassembly *disassembly = pool_alloc_type(pool, struct disassembly);
  memset(disassembly, 0, sizeof(struct disassembly));

  disassembly->pool = pool;
  disassembly->group = group;
  disassembly->class_def = class_def;
  disassembly->script = script;

  unsigned offset=0;
#define MAX_STACK 128
  struct instruction *stack[MAX_STACK];
  unsigned stack_ptr=0;

  struct statement *first_statement = NULL, *prev_statement = NULL, *statement = NULL;
  struct instruction **inst_ptr = &disassembly->instructions;
  unsigned debug_line=0;

  while(offset < script_def->body->code_size){

    if (debug_line+1 < script_def->body->debugline_count
      && offset >= script_def->body->debug_lines[debug_line].pcode_offset)
      debug_line++;

    const uint16_t *pc = (const uint16_t*)&script_def->body->code[offset];
    uint16_t opcode = pc[0];

    struct instruction *inst = (*inst_ptr) = pool_alloc_type(pool, struct instruction);
    memset(inst, 0, sizeof(struct instruction));
    inst->offset = offset;
    inst->opcode = opcode;
    inst->definition = PB120_opcodes[opcode];
    inst->args = pc+1;
    inst->next = NULL;
    inst->line_number = script_def->body->debug_lines[debug_line].line_number;
    inst_ptr = &inst->next;

    offset += (1+inst->definition->args)*2;

    init_stack(pool, inst, stack, &stack_ptr);

    DEBUGF(DISASSEMBLY,"%04x: %04x %s, %u, %u", inst->offset, opcode, inst->definition->name, inst->stack_count, stack_ptr);

    if (!statement){
      disassembly->statement_count++;
      statement = pool_alloc_type(pool, struct statement);
      memset(statement, 0, sizeof *statement);
      if (!first_statement){
	first_statement = statement;
      }else{
	prev_statement->next = statement;
	statement->prev = prev_statement;
      }
      statement->start = inst;
      statement->type=expression;
      inst->begin = 1;
      prev_statement = statement;
    }else
      inst->begin = 0;

    statement->end = inst;

    // first guess at flow control structure
    if (statement->type == expression){
      switch(inst->definition->id){
	case SM_JUMP_1:
	  statement->type=jump_goto;
	  break;

	case SM_JUMPTRUE_1:
	  if (inst->args[0] > inst->offset){
	    statement->type=do_until;
	  }else{
	    statement->type=loop_while;
	  }
	  break;

	case SM_JUMPFALSE_1:
	  if (inst->args[0] > inst->offset){
	    statement->type=if_then;
	  }else{
	    statement->type=loop_until;
	  }
	  break;

	case SM_PUSH_TRY_2:
	  statement->type=exception_try;
	  break;

	case SM_CATCH_EXCEPTION_0:
	  // this will be followed by a jump_if, but we can easily classify it now.
	  statement->type=exception_catch;
	  break;
      }
    }

    if (stack_ptr==0){
      // end of statement
      inst->end = 1;
      statement = NULL;
    }else
      inst->end = 0;
  }

  if (stack_ptr)
    WARNF("Stack pointer (%u) is not zero at the end of %s!", stack_ptr, script->name);

  // do we really want to fix this now? or insert goto destination, "do" & "end if" labels first?
  disassembly->statements = pool_alloc_array(pool, struct statement*, disassembly->statement_count+1);
  {
    struct statement *ptr = first_statement;
    unsigned i;
    for (i=0;i<disassembly->statement_count;i++){
      assert(ptr);
      disassembly->statements[i] = ptr;
      ptr = ptr->next;
    }
    disassembly->statements[disassembly->statement_count]=NULL;

    link_destinations(disassembly);
  }

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
  unsigned i;
  for (i=0;i<disassembly->statement_count;i++){
    dump_instruction(fd, disassembly->statements[i]->end);
    fprintf(fd, ";\n");
  }
}

void disassembly_free(struct disassembly *disassembly){
  pool_release(disassembly->pool);
}
