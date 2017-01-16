#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include "class_private.h"
#include "disassembly.h"
#include "debug.h"
#include "pool_alloc.h"

static const char *endif_label = "end if";
static const char *finally_label = "finally";
//static const char *do_label = "do";

static void dump_pcode_inst(FILE *fd, struct instruction *inst);
static void printf_instruction(FILE *fd, struct disassembly *disassembly, struct instruction *inst, uint8_t precedence);

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

// insert a lexical scope and update instructions to point to that scope
// a child scope must be completely contained within it's parent scope or the insert will fail
// Make sure to insert unambiguous scopes first, letting jumps that are likely to be actual goto's fall out last.
static struct scope *insert_scope(struct disassembly *disassembly, struct statement *start, struct statement *end){
  struct scope *parent_scope = start->scope;
  DEBUGF(DISASSEMBLY, "Attempting to insert scope from %u to %u", start->start_offset, end->end_offset);
  if (end->end_offset < start->start_offset){
    DEBUGF(DISASSEMBLY, "Ignoring new scope");
    return NULL;
  }

  // if multiple scopes already begin or end here, pop scopes that have a smaller range
  DEBUGF(DISASSEMBLY, "Current parent %p (%u %u)",
    parent_scope,
    parent_scope?parent_scope->start->start_offset:0,
    parent_scope?parent_scope->end->end_offset:0);

  while (parent_scope
    && (
      (parent_scope->start == start
      && parent_scope->end->end_offset < end->end_offset)
      ||
      (parent_scope->end == end
      && parent_scope->start->start_offset > start->start_offset)
    )){
    parent_scope = parent_scope->parent;
    DEBUGF(DISASSEMBLY, "Current parent %p (%u %u)",
      parent_scope,
      parent_scope?parent_scope->start->start_offset:0,
      parent_scope?parent_scope->end->end_offset:0);
  }

  // if the parent scope partially overlaps, we can't insert a new scope here
  if (parent_scope && parent_scope->end->end_offset < end->end_offset){
    DEBUGF(DISASSEMBLY, "Ignoring new scope (%p %u %u)", parent_scope, parent_scope->end->end_offset, end->end_offset);
    return NULL;
  }

  {
    // check the current scope at the end has the same parent
    struct scope *end_scope = end->scope;
    while(end_scope
      && end_scope != parent_scope
      && end_scope->end == end){
      end_scope = end_scope->parent;
    }
    if (end_scope != parent_scope){
      DEBUGF(DISASSEMBLY, "Ignoring new scope");
      return NULL;
    }
  }

  struct scope *scope = pool_alloc_type(disassembly->pool, struct scope);
  memset(scope, 0, sizeof *scope);
  scope->start = start;
  scope->end = end;
  scope->parent = parent_scope;

  struct statement *ptr = start;
  while(ptr){
    if (ptr->scope == parent_scope){
      ptr->scope = scope;
    }else if (ptr->scope && ptr->scope->parent == parent_scope){
      DEBUGF(DISASSEMBLY, "Reparent scope %p from %p to %p", ptr->scope, ptr->scope->parent, scope);
      ptr->scope->parent = scope;
    }
    if (ptr == end)
      break;
    ptr = ptr->next;
  }

  DEBUGF(DISASSEMBLY, "Scope %p added with parent %p", scope, parent_scope);
  return scope;
}

// a forwards jumpfalse might be for_next, do_while or just if_then
static void classify_if_then(struct disassembly *disassembly, unsigned statement_number)
{
  struct statement *if_test = disassembly->statements[statement_number];
  if (!if_test->branch)
    return;
  struct statement *prior = if_test->branch->prev;

  // do loops end with a jump back to the start
  if (prior && prior->type == jump_goto){
    unsigned dest_offset = prior->end->args[0];
    if (dest_offset == if_test->start->offset){
      if (if_test->type == jump_false)
	if_test->type = do_while;
      else
	if_test->type = do_until;

      // loop body
      struct scope *scope = insert_scope(disassembly, if_test->next, prior->prev);
      assert(scope);
      scope->indent = 1;
      scope->break_dest = if_test->branch;
      scope->continue_dest = prior;

      prior->type = jump_loop;
      if_test->branch->classified_count++;
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

    if (if_test->type == jump_false
      && statement_number >= 3
      && disassembly->statements[statement_number -3]->start_line_number == if_test->end_line_number
      && dest_offset == disassembly->statements[statement_number -1]->start->offset
      && disassembly->statements[statement_number -2]->type == jump_goto
      && disassembly->statements[statement_number -2]->end->args[0] == if_test->start->offset
      // with a C style for loop, this would be enough. For PB's basic style we should be more explicit;
      // && statement_number -3 == SM_ASSIGN_[TYPE]
      // && statement_number -1 == SM_INCR_[TYPE] || SM_ADDASSIGN_[TYPE]
      // && same variable in all cases
      ){
      disassembly->statements[statement_number -3]->type = for_init;
      disassembly->statements[statement_number -2]->type = for_jump;
      disassembly->statements[statement_number -1]->type = for_step;
      if_test->type = for_test;
      if_test->classified_count++;
      disassembly->statements[statement_number -1]->classified_count++;

      // loop body
      struct scope *scope = insert_scope(disassembly, if_test->next, prior->prev);
      assert(scope);
      scope->indent = 1;
      scope->break_dest = if_test->branch;
      scope->continue_dest = prior;

      prior->type = jump_next;
      if_test->branch->classified_count++;
      return;
    }
  }

  if (if_test->type == jump_false){
    // one line if test?
    unsigned i=statement_number+1;
    // TODO, only skip generated statements? (eg SM_JUMP_1 to SM_RETURN_0)
    while(i < disassembly->statement_count && disassembly->statements[i]->end_line_number == if_test->start_line_number)
      i++;
    if (i>statement_number+1 && if_test->branch == disassembly->statements[i]){
      if_test->type = if_then;
      if_test->branch->classified_count++;
      return;
    }
  }

  if(if_test->type == jump_true
    && disassembly->statements[disassembly->statement_count -1]->end_line_number == if_test->start_line_number){
    // guessing that this looks like generated code to return message.returnvalue at the end of an event
    // TODO more explicit matching of the expected code sequence.
    unsigned i;
    if_test->branch->classified_count++;
    for(i=statement_number; i<disassembly->statement_count; i++){
      disassembly->statements[i]->type = generated;
    }
    return;
  }

  if (if_test->type == jump_false){
    struct scope *scope = insert_scope(disassembly, if_test->next, prior);
    if (scope){
      scope->indent = 1;
      if_test->type = if_then;
      if_test->branch->classified_count++;
      scope->end_label = endif_label;
    }
  }
}

static void link_destinations(struct disassembly *disassembly){
  unsigned i;
  // find the destination of each jump
  fflush(stdout);
  for (i=0;i<disassembly->statement_count;i++){
    struct statement *ptr = disassembly->statements[i];
    switch(ptr->type){
      case exception_try:{
	DEBUGF(DISASSEMBLY, "Try?");
	unsigned catch_offset = ptr->end->args[0];
	unsigned end_offset = ptr->end->args[1];
	struct statement *catch_block=NULL, *end=NULL;
	struct statement *finally=NULL, *end_finally=NULL;
	unsigned j;

	// TODO binary search?
	for (j=i+1;j<disassembly->statement_count;j++){
	  if (disassembly->statements[j]->start_offset == catch_offset){
	    catch_block = disassembly->statements[j];
	  }
	  if (disassembly->statements[j]->start_offset == end_offset){
	    end = disassembly->statements[j];
	  }
	  if (disassembly->statements[j]->end->definition->id == SM_JUMP_1
	   && disassembly->statements[j]->end->args[0] == end_offset)
	     disassembly->statements[j]->type = generated;
	}
	assert(catch_block && end);
	struct statement *try_end = catch_block->prev;

	if (end->end->definition->id == SM_GOSUB_1){
	  DEBUGF(DISASSEMBLY, "has finally?");
	  unsigned finally_offset = end->end->args[0];
	  for (j=i+1;j<disassembly->statement_count;j++){
	    if (disassembly->statements[j]->start->offset == finally_offset){
	      finally = disassembly->statements[j];
	      end_finally = end->prev;

	      if (finally_offset < catch_offset)
		try_end = finally->prev;
	      break;
	    }
	  }
	  end->type = generated;
	  end = end->next;
	}
	assert(end->end->definition->id == SM_POP_TRY_0);
	end->type = exception_end_try;

	assert(try_end->end->definition->id == SM_JUMP_1);
	assert(try_end->end->args[0] == end_offset);
	
	// indent the guarded code (should be some??)
	if (ptr != try_end){
	  struct scope *scope = insert_scope(disassembly, ptr->next, try_end);
	  scope->indent = 1;
	}
	// scope for the finally block (if any)
	if (finally){
	  struct scope *scope = insert_scope(disassembly, finally, end_finally);
	  scope->indent = 1;
	  scope->begin_label = finally_label;
	}
	// the catch block should look like a standard if then elseif... so we don't need a special case here

	// scope for the whole try / end try block
	insert_scope(disassembly, ptr, end);
	continue;
      }
      case exception_catch:
	assert(ptr->end->definition->id == SM_JUMPFALSE_1);
	goto find_dest;

      case jump_true:
      case jump_false:
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
	continue;
    }

    if (ptr->type == jump_goto && ptr->branch->end->definition->id == SM_RETURN_0){
      ptr->type = generated;
      ptr->branch->classified_count++;
    }

    if (ptr->type == jump_true || ptr->type == jump_false)
      classify_if_then(disassembly, i);
  }

  // work backwards to simplify merging elseif's as we go.
  for (i=disassembly->statement_count;i>1;i--){
    struct statement *jmp = disassembly->statements[i - 1];
    if (jmp->type == jump_goto && jmp->branch){
      // try to identify else's and elseif's...
      if (jmp->scope
      && jmp->start->args[0] > jmp->start_offset
      && jmp->scope->end_label == endif_label
      && jmp->scope->end == jmp){
	struct statement *nxt = jmp->next;

	if (nxt->type == if_then
	  && nxt->start_line_number == jmp->start_line_number){

	  struct statement *possible_else = nxt->branch->prev;
	  if ((possible_else->type == jump_else || possible_else->type == jump_elseif)
	    && possible_else->branch == jmp->branch){
	    // elseif with another else or elseif
	    jmp->type = jump_elseif;

	  }else if(nxt->branch == jmp->branch){
	    // last elseif with no else
	    jmp->type = jump_elseif;
	  }
	}

	if (jmp->type == jump_goto){
	  // else?
	  struct scope *scope = insert_scope(disassembly, nxt, jmp->branch->prev);
	  if (scope){
	    scope->end_label = endif_label;
	    scope->indent = 1;
	    jmp->type = jump_else;
	  }
	}

	// any of the above, shrink the if test scope to exclude the else and drop the end if label
	if (jmp->type != jump_goto){
	  jmp->branch->classified_count ++;
	  struct scope *scope = jmp->scope;
	  scope->end = jmp->prev;
	  scope->end_label = NULL;
	  jmp->scope = scope->parent;
	  continue;
	}
      }

      // try to reclassify goto's as continue, exit
      {
	struct scope *scope = jmp->scope;
	while(scope && !scope->continue_dest)
	  scope = scope->parent;
	if (scope && jmp->branch == scope->break_dest){
	  jmp->type = jump_exit;
	  jmp->branch->classified_count++;
	  continue;
	}
	if (scope && jmp->branch == scope->continue_dest){
	  jmp->type = jump_continue;
	  jmp->branch->classified_count++;
	  continue;
	}
      }
    }
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
      statement->start_line_number = inst->line_number;
      statement->end_line_number = inst->line_number;
      statement->start_offset = inst->offset;
      statement->type=expression;
      inst->begin = 1;
      prev_statement = statement;
    }else{
      inst->begin = 0;

      if (inst->line_number < statement->start_line_number)
	statement->start_line_number = inst->line_number;
      if (inst->line_number > statement->end_line_number)
	statement->end_line_number = inst->line_number;
    }

    statement->end = inst;
    statement->end_offset = offset -1;

    // first guess at flow control structure
    if (statement->type == expression){
      switch(inst->definition->id){
	//case SM_GOSUB_1:
	//case SM_POP_TRY_0: // only the last one will end up as exception_end_try
	case SM_RETURN_SUB_0:
	case SM_RETURN_0:
	  statement->type=generated;
	  break;

	case SM_JUMP_1:
	  statement->type=jump_goto;
	  break;

	case SM_JUMPTRUE_1:
	  if (inst->args[0] > inst->offset){
	    statement->type=jump_true;
	  }else{
	    statement->type=loop_while;
	  }
	  break;

	case SM_JUMPFALSE_1:
	  if (inst->args[0] > inst->offset){
	    statement->type=jump_false;
	  }else{
	    statement->type=loop_until;
	  }
	  break;

	case SM_PUSH_TRY_2:
	  statement->type=exception_try;
	  break;

	//case SM_CATCH_EXCEPTION_0:
	  // this will be followed by a jump_if, but we can easily classify it now.
	  //statement->type=exception_catch;
	  //break;

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
      fprintf(stderr," [");
      printf_instruction(stderr, disassembly, inst, 0);
      fprintf(stderr,"]\n");
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

  fprintf(fd, "%s [%u](", inst->definition->name, inst->definition->precedence);
  for (arg=0; arg < inst->stack_count; arg++){
    if (arg>0)
      fprintf(fd, ", ");
    if (inst->stack && inst->stack[arg])
      fprintf(fd, "%04x %s [%u]", inst->stack[arg]->offset, inst->stack[arg]->definition->name, inst->stack[arg]->definition->precedence);
    else
      fprintf(fd, "NULL");
  }
  fprintf(fd, ")");
  if (inst->begin)
    fprintf(fd, " BEGIN");
  if (inst->end)
    fprintf(fd, " END");
}

void dump_pcode(FILE *fd, struct disassembly *disassembly){
  unsigned i;
  for(i=0;i<disassembly->instruction_count;i++){
    struct instruction *inst = disassembly->instructions[i];
    dump_pcode_inst(fd, inst);
    fprintf(fd, "\n");
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
    case 1:{
      fprintf(fd, "%d", *(const int*)ptr);
      return;
    }
    case 4:{
      fprintf(fd, "%f", *(const double*)ptr);
      return;
    }
    case 5:{
      // decimal
      intmax_t magnitude=0; // probably not big enough, but should work for smaller constants.
      uint8_t sign;
      uint8_t exponent;
      if (group->header.compiler_version <= PB10){
	struct pb_old_decimal *dec = (struct pb_old_decimal *)ptr;
	sign = dec->sign;
	exponent = dec->exponent;
	memcpy(&magnitude, dec->magnitude, sizeof magnitude > sizeof dec->magnitude ? sizeof dec->magnitude : sizeof magnitude);
      }else{
	struct pb_decimal *dec = (struct pb_decimal *)ptr;
	sign = dec->sign;
	exponent = dec->exponent;
	memcpy(&magnitude, dec->magnitude, sizeof magnitude > sizeof dec->magnitude ? sizeof dec->magnitude : sizeof magnitude);
      }
      if (sign)
	magnitude = -magnitude;
      char buff[32];
      int chars = snprintf(buff, sizeof buff, "%"PRIdMAX, magnitude);
      if (exponent){
	unsigned i;
	for(i=0;i<exponent;i++)
	  buff[chars -i] = buff[chars -i -1];
	buff[chars - exponent]='.';
	buff[chars+1]=0;
      }
      fputs(buff, fd);
      return;
    }
    case 6: {
      const struct pb_datetime *datetime = ptr;
      // probably enough to distinguish dates and times...
      if (datetime->year == 63636 && datetime->month == 255){
	fprintf(fd, "%02d:%02d:%02d.%06d",
	  datetime->hour,
	  datetime->minute,
	  datetime->second,
	  datetime->millisecond);
      }else{
	fprintf(fd, "%04d-%02d-%02d",
	  datetime->year + 1900,
	  datetime->month + 1,
	  datetime->day);
      }
      return ;
    }
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

static void printf_instruction(FILE *fd, struct disassembly *disassembly, struct instruction *inst, uint8_t precedence){
  if (!inst){
    fprintf(fd, "***NULL***");
    return;
  }
  unsigned i;
  const char **tokens=inst->definition->tokens;
  struct class_group_private *group = (struct class_group_private *)disassembly->group;
  //struct class_def_private *class_def = (struct class_def_private *)disassembly->class_def;
  struct script_def_private *script = (struct script_def_private *)disassembly->script;

  uint8_t this_precedence = inst->definition->precedence;

  if (!*tokens){
    fputs(inst->definition->name, fd);
    if (inst->definition->args){
      fputc('[', fd);
      for (i=0; i < inst->definition->args; i++){
	if (i>0)
	  fputs(", ", fd);
	fprintf(fd, "%04x", inst->args[i]);
      }
      fputc(']', fd);
    }
    fputc('(', fd);
    for (i=0; i < inst->stack_count; i++){
      if (i>0)
	fputs(", ", fd);
      printf_instruction(fd, disassembly, inst->stack[inst->stack_count - i - 1], this_precedence);
    }
    fputc(')', fd);
    if (inst->end)
      fputc(';', fd);
    return;
  }

  if (precedence && precedence < this_precedence)
    fputs("(", fd);

  while(*tokens){
    //fprintf(fd, "\n%s %p %u\n", inst->definition->name, *tokens, inst->stack_count);
    switch((enum token_types)(*tokens)){
      case STACK:{
	i=(int)(*(++tokens));
	assert(i<inst->stack_count);
	uint8_t p = this_precedence;
	// decrement precendence to detect left to right rule violation, eg a - (b + c)
	if (p && inst->stack_count==2 && i==0)
	  p--;
	printf_instruction(fd, disassembly, inst->stack[i], p);
	break;
      }

      case STACK_CSV:
	for (i=0; i < inst->stack_count; i++){
	  if (i>0)
	    fputs(", ", fd);
	  printf_instruction(fd, disassembly, inst->stack[inst->stack_count - i - 1], this_precedence);
	}
	break;

      case STACK_DOT_CSV:
	for (i=1; i < inst->stack_count; i++){
	  if (i>1)
	    fputs(", ", fd);
	  printf_instruction(fd, disassembly, inst->stack[inst->stack_count - i], this_precedence);
	}
	break;

      case LOCAL:{
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	unsigned var = inst->args[i];
	assert(var<disassembly->script->local_variable_count);
	fputs(disassembly->script->local_variables[var]->name, fd);
      }break;

      case SHARED:
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	i = inst->args[i];
	assert(i<disassembly->group->global_variable_count);
	fputs(disassembly->group->global_variables[i]->name, fd);
	break;

      case TYPE:{
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	uint16_t type = inst->args[i];
	fputs(get_type_name(group, type), fd);
      }break;

      case ARG_CSV:
	for (i=0; i < inst->definition->args; i++){
	  if (i>0)
	    fputs(", ", fd);
	  fprintf(fd, "%04x", inst->args[i]);
	}
	break;

      case ARG_BOOL:
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	fprintf(fd, "%s", inst->args[i] ? "true" : "false");
	break;

      case GLOBAL:
      case ARG_INT:
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	fprintf(fd, "%d", inst->args[i]);
	break;

      case METHOD_FLAGS:{
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	uint16_t flags = inst->args[i];
	if (flags & 1)
	  fputs("post ", fd);
	if (flags & 2)
	  fputs("dynamic ", fd);
      }break;

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
	fputc('\"', fd);
	while(*str){
	  switch(*str){
	    case '\"': fputs("~\"", fd); break;
	    case '\'': fputs("~\'", fd); break;
	    case '\r': fputs("~r", fd); break;
	    case '\n': fputs("~n", fd); break;
	    case '~': fputs("~~", fd); break;
	    default: fputc(*str, fd); break;
	  }
	  str++;
	}
	fputc('\"', fd);
      }break;
      case RES_STRING:{
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	uint32_t offset = *(const uint32_t*)&inst->args[i];
	fputs(get_table_string(group, &script->body->resources, offset), fd);
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
	fputc(';', fd);
	break;

      default:
	fputs(*tokens, fd);
    }
    tokens++;
  }

  if (precedence && precedence < this_precedence)
    fputs(")", fd);
}

static void fputeol(FILE *fd, int i){
  fputc('\n', fd);
  while(i-->0)
    fputs("    ", fd);
}

void dump_statements(FILE *fd, struct disassembly *disassembly){
  if (IFDEBUG(DISASSEMBLY))
    dump_script_resources(fd, disassembly->group, disassembly->script);
  unsigned i;
  unsigned line=1;
  int indent=0;
  struct scope *current_scope = NULL;

  for (i=0;i<disassembly->statement_count;i++){
    struct statement *statement = disassembly->statements[i];
    //dump_instruction(fd, statement->end);
    //fprintf(fd, ";\n");

    if (statement->scope != current_scope){
      struct scope *entering = statement->scope;
      while(entering!=current_scope){
	if (!entering){
	  fflush(fd);
	  DEBUGF(DISASSEMBLY, "Did not find parent scope %p!", current_scope);
	}
	assert(entering);

	if (entering->begin_label){
	  if (line < statement->start_line_number){
	    line++;
	    fputeol(fd, indent);
	  }
	  fprintf(fd, "%s;", entering->begin_label);
	}
	if (entering->indent)
	  indent++;
	entering = entering->parent;
      }
      current_scope = statement->scope;
    }

    assert(indent>=0);

    if (statement->classified_count < statement->destination_count){
      fprintf(fd, "Offset_%u:", statement->start->offset);
      if (line < statement->start_line_number){
	line++;
	fputeol(fd, indent);
      }
    }

    if (IFDEBUG(DISASSEMBLY) || statement->type != generated){
      while (line < statement->start_line_number){
	line++;
	fputeol(fd, indent);
      }

      // special cases;
      switch(statement->type){
	case exception_try:	fputs("try;", fd); break;
	case exception_end_try:	fputs("end try;", fd); break;
	case jump_loop:		fputs("loop;", fd); break;
	case jump_next:		fputs("next;", fd); break;
	case jump_exit:		fputs("exit;", fd); break;
	case jump_continue:	fputs("continue;", fd); break;
	case jump_else:		fputs("else;", fd); break;
	case jump_elseif:		fputs("else", fd); break;
	case if_then:
	  fputs("if ", fd);
	  printf_instruction(fd, disassembly, statement->end->stack[0], 0);
	  fputs(" then ", fd);
	  break;
	case do_while:
	  fputs("do while ", fd);
	  printf_instruction(fd, disassembly, statement->end->stack[0], 0);
	  fputs(";", fd);
	  break;
	case do_until:
	  fputs("do until ", fd);
	  printf_instruction(fd, disassembly, statement->end->stack[0], 0);
	  fputs(";", fd);
	  break;
	case loop_while:
	  fputs("loop while ", fd);
	  printf_instruction(fd, disassembly, statement->end->stack[0], 0);
	  fputs(";", fd);
	  break;
	case loop_until:
	  fputs("loop until ", fd);
	  printf_instruction(fd, disassembly, statement->end->stack[0], 0);
	  fputs(";", fd);
	  break;
	case for_init:
	  fputs("for(", fd);
	  printf_instruction(fd, disassembly, statement->end, 0);
	  fputc(' ', fd);
	  printf_instruction(fd, disassembly, disassembly->statements[i+3]->end->stack[0], 0);
	  fputs("; ", fd);
	  printf_instruction(fd, disassembly, disassembly->statements[i+2]->end, 0);
	  fputc(')', fd);
	  i+=3;// skip other instructions completely
	  break;
	case mem_append:{
	  struct instruction *lhs = statement->end->stack[1];
	  struct instruction *rhs = statement->end->stack[0]->stack[0];
	  printf_instruction(fd, disassembly, lhs, 0);
	  fputs(" += ", fd);
	  printf_instruction(fd, disassembly, rhs, 0);
	  fputc(';', fd);
	}break;
	default:
	  printf_instruction(fd, disassembly, statement->end, 0);
	  break;
      }

      // For now just add comments for known special cases;
      switch(statement->type){
	case generated:		fprintf(fd, " /* GENERATED? */"); break;
	default: break;
      }
    }

    while (current_scope && current_scope->end == statement){
      if (current_scope->indent)
	indent--;
      if (current_scope->end_label){
	if (!statement->next || line < statement->next->start_line_number){
	  line++;
	  fputeol(fd, indent);
	}
	fprintf(fd, "%s;", current_scope->end_label);
      }
      current_scope = current_scope->parent;
    }
  }
}

void disassembly_free(struct disassembly *disassembly){
  pool_release(disassembly->pool);
}
