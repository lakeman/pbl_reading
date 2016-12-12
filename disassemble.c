#include <assert.h>
#include <string.h>
#include "class_private.h"
#include "disassembly.h"
#include "debug.h"
#include "pool_alloc.h"

static void dump_pcode_inst(FILE *fd, struct instruction *inst);

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
      {
	// is this close enough?
	struct instruction *tmp = POP();
	PUSH(inst);
	PUSH(tmp);
      }
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
  if (!if_test->branch)
    return;
  struct statement *prior = if_test->branch->prev;

  // loops end with a jump back to the start
  if (prior && prior->type == jump_goto){
    unsigned dest_offset = prior->end->args[0];
    if (dest_offset == if_test->start->offset){
      if (if_test->type == if_then_endif)
	if_test->type = do_while;
      else
	if_test->type = do_until;
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

    if (if_test->type == if_then_endif
      && statement_number >= 3
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

  if (if_test->type == if_then_endif){
    // one line if test?
    unsigned i=statement_number+1;
    while(i < disassembly->statement_count && disassembly->statements[i]->end->line_number == if_test->end->line_number)
      i++;
    if (i>statement_number+1 && if_test->branch == disassembly->statements[i]){
      if_test->type = if_then;
      return;
    }
  }

  if(if_test->type == if_not_then
    && disassembly->statements[disassembly->statement_count -1]->end->line_number == if_test->start->line_number){
    // generated code at the end of an event?
    unsigned i;
    for(i=statement_number; i<disassembly->statement_count; i++){
      disassembly->statements[i]->type = generated;
    }
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

      case if_then_endif:
      case if_not_then:
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
	if (!ptr->branch){
	  DEBUGF(DISASSEMBLY, "Branch dest %04x is not the start of a statement (for %s @%04x)", dest_offset, ptr->end->definition->name, ptr->end->offset);
	  continue;
	}
      } break;

      default:
	break;
    }

    if (ptr->type == jump_goto && ptr->branch->type == generated)
      ptr->type = generated;

    if (ptr->type == if_then_endif || ptr->type == if_not_then)
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
  unsigned debug_line=0;

  unsigned instruction_count=0;
  struct instruction *instructions[script_def->body->code_size/2];

  while(offset < script_def->body->code_size){

    if (debug_line+1 < script_def->body->debugline_count
      && offset >= script_def->body->debug_lines[debug_line+1].pcode_offset)
      debug_line++;

    const uint16_t *pc = (const uint16_t*)&script_def->body->code[offset];
    uint16_t opcode = pc[0];

    struct instruction *inst = instructions[instruction_count++] = pool_alloc_type(pool, struct instruction);
    memset(inst, 0, sizeof(struct instruction));
    inst->offset = offset;
    inst->opcode = opcode;
    inst->definition = PB120_opcodes[opcode];
    inst->args = pc+1;
    inst->line_number = script_def->body->debug_lines[debug_line].line_number;

    offset += (1+inst->definition->args)*2;

    init_stack(pool, inst, stack, &stack_ptr);

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
	case SM_RETURN_0:
	  statement->type=generated;
	  break;

	case SM_JUMP_1:
	  statement->type=jump_goto;
	  break;

	case SM_JUMPTRUE_1:
	  if (inst->args[0] > inst->offset){
	    statement->type=if_not_then;
	  }else{
	    statement->type=loop_while;
	  }
	  break;

	case SM_JUMPFALSE_1:
	  if (inst->args[0] > inst->offset){
	    statement->type=if_then_endif;
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

	case SM_ASSIGN_BLOB_1:
	case SM_ASSIGN_STRING_1:{
	  struct instruction *cat;
	  if (inst->stack_count == 2
	    && ((cat = inst->stack[0])->definition->id == SM_CAT_STRING_0
	      || cat->definition->id == SM_CAT_BINARY_0)
	    && cat->stack_count == 2
	    && cat->stack[1]->definition->id == SM_DUP_STACKED_LVALUE_1
	    )
	    statement->type=mem_append;
	}break;
      }
    }

    if (stack_ptr==0){
      // end of statement
      inst->end = 1;
      statement = NULL;
    }else
      inst->end = 0;

    if (IFDEBUG(DISASSEMBLY)){
      fflush(stdout);
      dump_pcode_inst(stderr, inst);
    }
  }

  if (stack_ptr)
    WARNF("Stack pointer (%u) is not zero at the end of %s!", stack_ptr, script->name);

  disassembly->instruction_count = instruction_count;
  disassembly->instructions = pool_alloc_array(pool, struct instruction *, instruction_count+1);
  memcpy(disassembly->instructions, instructions, instruction_count * sizeof(struct instruction *));
  disassembly->instructions[instruction_count]=NULL;

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

static void dump_pcode_inst(FILE *fd, struct instruction *inst){
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
  fprintf(fd, ")");
  if (inst->begin)
    fprintf(fd, " BEGIN");
  if (inst->end)
    fprintf(fd, " END");
  fprintf(fd, "\n");
}

void dump_pcode(FILE *fd, struct disassembly *disassembly){
  unsigned i;
  for(i=0;i<disassembly->instruction_count;i++){
    struct instruction *inst = disassembly->instructions[i];
    dump_pcode_inst(fd, inst);
  }
}

/*
static void dump_instruction(FILE *fd, struct instruction *inst){
  if (!inst){
    fprintf(fd, "***NULL***");
    return;
  }
  unsigned i;

  fprintf(fd, "%s(", inst->definition->name);
  for (i=0; i < inst->stack_count; i++){
    if (i>0)
      fprintf(fd, ", ");
    dump_instruction(fd, inst->stack[i]);
  }
  fprintf(fd, ")");
}
*/

static void printf_res(FILE *fd, struct class_group_private *group, struct data_table *table, uint32_t offset){
  const void *ptr = get_table_ptr(group, table, offset);
  if (!ptr){
    fprintf(fd, "**NULL**");
    return;
  }

  const struct pbtable_info *info = get_table_info(group, table, offset);
  if (!info){
    fprintf(fd, "**INFO=NULL**");
    return;
  }

  switch(info->structure_type){
    case 12:{ // property reference
      const struct pbprop_ref *ref = (struct pbprop_ref *)ptr;
      const char *name = get_table_string(group, table, ref->name_offset);
      if (name)
	fprintf(fd, "%s", name);
      else
	fprintf(fd, "prop_%u", ref->prop_number);
      return;
    }
    case 13:{ // method reference
      const struct pbmethod_ref *ref = (struct pbmethod_ref *)ptr;
      const char *name = get_table_string(group, table, ref->name_offset);
      if (name)
	fprintf(fd, "%s", name);
      else
	fprintf(fd, "method_%u", ref->method_number);
      return;
    }
    case 18:{
      const struct pbcreate_ref *ref = (struct pbcreate_ref *)ptr;
      const char *name = get_table_string(group, table, ref->name_offset);
      if (name)
	fprintf(fd, "%s", name);
      else
	fprintf(fd, "%s", get_type_name(group, ref->type));
      return;
    }
  }
  fprintf(fd, "%02x_%04x", info->structure_type, offset);
}

static void printf_instruction(FILE *fd, struct disassembly *disassembly, struct instruction *inst){
  if (!inst){
    fprintf(fd, "***NULL***");
    return;
  }
  unsigned i;
  const char **tokens=inst->definition->tokens;
  struct class_group_private *group = (struct class_group_private *)disassembly->group;
  //struct class_def_private *class_def = (struct class_def_private *)disassembly->class_def;
  struct script_def_private *script = (struct script_def_private *)disassembly->script;

  if (!*tokens){
    fprintf(fd, "%s[", inst->definition->name);
    for (i=0; i < inst->definition->args; i++){
      if (i>0)
	fprintf(fd, ", ");
      fprintf(fd, "%04x", inst->args[i]);
    }
    fprintf(fd, "](");
    for (i=0; i < inst->stack_count; i++){
      if (i>0)
	fprintf(fd, ", ");
      printf_instruction(fd, disassembly, inst->stack[inst->stack_count - i - 1]);
    }
    fprintf(fd, ")");
    return;
  }

  while(*tokens){
    //fprintf(fd, "\n%s %p %u\n", inst->definition->name, *tokens, inst->stack_count);
    switch((enum token_types)(*tokens)){
      case STACK:
	i=(int)(*(++tokens));
	assert(i<inst->stack_count);
	printf_instruction(fd, disassembly, inst->stack[i]);
	break;

      case STACK_CSV:
	for (i=0; i < inst->stack_count; i++){
	  if (i>0)
	    fprintf(fd, ", ");
	  printf_instruction(fd, disassembly, inst->stack[inst->stack_count - i - 1]);
	}
	break;

      case STACK_DOT_CSV:
	for (i=1; i < inst->stack_count; i++){
	  if (i>1)
	    fprintf(fd, ", ");
	  printf_instruction(fd, disassembly, inst->stack[inst->stack_count - i]);
	}
	break;

      case LOCAL:{
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	unsigned var = inst->args[i];
	assert(var<disassembly->script->local_variable_count);
	fprintf(fd, "%s", disassembly->script->local_variables[var]->name);
      }break;

      case SHARED:
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	i = inst->args[i];
	assert(i<disassembly->group->global_variable_count);
	fprintf(fd, "%s", disassembly->group->global_variables[i]->name);
	break;

      case TYPE:{
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	uint16_t type = inst->args[i];
	fprintf(fd, "%s", get_type_name(group, type));
      }break;

      case ARG_CSV:
	for (i=0; i < inst->definition->args; i++){
	  if (i>0)
	    fprintf(fd, ", ");
	  fprintf(fd, "%04x", inst->args[i]);
	}
	break;

      case GLOBAL:
      case ARG_INT:
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	fprintf(fd, "%d", inst->args[i]);
	break;

      case RES:{
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	printf_res(fd, group, &script->body->resources, *(const uint32_t*)&inst->args[i]);
      }break;

      case RES_STRING_CONST:{
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	uint32_t offset = *(const uint32_t*)&inst->args[i];
	const char *str = get_table_string(group, &script->body->resources, offset);
	// TODO escape each character...
	fprintf(fd, "\"%s\"", str);
      }break;
      case RES_STRING:{
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	uint32_t offset = *(const uint32_t*)&inst->args[i];
	fprintf(fd, "%s", get_table_string(group, &script->body->resources, offset));
      }break;

      case ARG_LONG_HEX:
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	fprintf(fd, "%08x", *(const uint32_t*)&inst->args[i]);
	break;

      case ARG_LONG:
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	fprintf(fd, "%d", *(const uint32_t*)&inst->args[i]);
	break;

      case FUNC_CLASS:{
	uint16_t type = inst->args[1];
	uint16_t id = inst->args[0];
	if ((type & 0xC000) == 0x8000){
	  struct class_def_private *class_def = get_class_by_type(group, type);
	  if (class_def){
	    assert(id < class_def->variables.count);
	    fprintf(fd, "%s", class_def->variables.names[id]);
	    break;
	  }
	}
	fprintf(fd, "%s_%u", get_type_name(group, type), id);
      }break;

      case END:
	fprintf(fd,";");
	break;

      default:
	fprintf(fd, "%s", *tokens);
    }
   tokens++;
  }
}

void dump_statements(FILE *fd, struct disassembly *disassembly){
  if (IFDEBUG(DISASSEMBLY))
    dump_script_resources(fd, disassembly->group, disassembly->script);
  unsigned i;
  unsigned line=1;
  for (i=0;i<disassembly->statement_count;i++){
    struct statement *statement = disassembly->statements[i];
    //dump_instruction(fd, statement->end);
    //fprintf(fd, ";\n");
    if (statement->type == generated)
      continue;

    while (line < statement->start->line_number){
      fprintf(fd, "\n");
      line++;
    }

    if (statement->destination_count)
      fprintf(fd, "Offset_%u:\n", statement->start->offset);

    printf_instruction(fd, disassembly, statement->end);

    // For now just add comments for known special cases;
    switch(statement->type){
      case if_then:		fprintf(fd, " /* IF [condition] THEN ... */"); break;
      case if_then_endif:	fprintf(fd, " /* IF [condition] THEN ... END IF */"); break;
      case do_while:		fprintf(fd, " /* DO WHILE */"); break;
      case if_not_then:		fprintf(fd, " /* IF NOT ? */"); break;
      case do_until:		fprintf(fd, " /* DO UNTIL */"); break;
      case loop_while:		fprintf(fd, " /* LOOP WHILE */"); break;
      case loop_until:		fprintf(fd, " /* LOOP UNTIL */"); break;
      case loop:		fprintf(fd, " /* LOOP */"); break;
      //case for_init:		fprintf(fd, " /* FOR"); break;
      //case for_jump:		fprintf(fd, " /* FOR"); break;
      //case for_step:		fprintf(fd, " /* FOR"); break;
      case for_test:		fprintf(fd, " /* FOR */"); break;
      case mem_append:		fprintf(fd, " /* += */"); break;
      default: break;
    }
  }
}

void disassembly_free(struct disassembly *disassembly){
  pool_release(disassembly->pool);
}
