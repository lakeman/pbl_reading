#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include "class_private.h"
#include "disassembly.h"
#include "debug.h"
#include "pool_alloc.h"

/* Outline of the disassembly process;
 *
 * - identify each instruction and the number of arguments it has
 *
 * - emulate the effect each instruction has on the stack so we can link them together in something like SSA form
 *
 * - identify the start and end of each "statement", based on when the stack is empty
 *   (or perhaps by knowing which instructions are supposed to end a statement)
 *
 * - identify control flow that has an unambiguous source code representation (if then, do until, loop (while|until), try, ... )
 *
 * - attempt to classify ambiguous control flow (for, do while, else, elseif, continue, exit, ...)
 *
 * - insert scope objects to describe indentation regions and where additional labels should be inserted (end if, do, finally)
 *   since scope objects must stack without overlapping, the order of scope object creation is used to reject
 *   interpretations of control flow with multiple possible representations where this would create an inconsistency.
 *
 * Hopefully, all conditional branches will be correctly identified. With unconditional goto's remaining only where they
 * existed in the original source code.
 */

static const char *endif_label = "end if";
static const char *finally_label = "finally";
static const char *do_label = "do";

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
static struct scope *insert_scope(struct disassembly *disassembly,
  struct statement *start, struct statement *indent_start,
  struct statement *indent_end, struct statement *end){

  assert(start->start_offset < end->end_offset);
  assert((indent_start ? 1 : 0) == (indent_end ? 1 : 0));
  // Don't indent if the range is empty
  if (indent_start && indent_start->start_offset > indent_end->end_offset)
    indent_start = indent_end = NULL;

  struct scope *parent_scope = start->scope;
  DEBUGF(DISASSEMBLY, "Attempting to insert scope from %u to %u", start->start_offset, end->end_offset);

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
  scope->indent_start = indent_start;
  scope->indent_end = indent_end;
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

  // do ... loop (while|until), the conditional branch jumps back to the start
  if (if_test->branch->start_offset < if_test->start_offset){
    struct scope *scope = insert_scope(disassembly, if_test->branch, if_test->branch, if_test->prev, if_test);
    if (scope){
      if (if_test->type == jump_false)
	if_test->type = loop_until;
      else
	if_test->type = loop_while;
      scope->begin_label = do_label;
      scope->break_dest = if_test->next;
      scope->continue_dest = if_test;
      if_test->branch->classified_count++;
      return;
    }
  }

  struct statement *prior = if_test->branch->prev;

  // do (while|until) ... loop, ends with a jump back to the start
  if (if_test->branch->start_offset > if_test->start_offset
    && prior && prior->type == jump_goto){

    unsigned dest_offset = prior->end->args[0];
    if (dest_offset == if_test->start->offset){
      if (if_test->type == jump_false)
	if_test->type = do_while;
      else
	if_test->type = do_until;

      // loop body
      struct scope *scope = insert_scope(disassembly, if_test, if_test->next, prior->prev, prior);
      assert(scope);
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

    struct statement *init;
    struct statement *step;
    struct statement *jmp;

    if (if_test->type == jump_false
      && if_test->branch->start_offset > if_test->start_offset
      && statement_number >= 3
      && (init = disassembly->statements[statement_number -3])->start_line_number == if_test->end_line_number
      && dest_offset == (step = disassembly->statements[statement_number -1])->start->offset
      && (jmp = disassembly->statements[statement_number -2])->type == jump_goto
      && jmp->end->args[0] == if_test->start->offset
      // with a C style for loop, this would be enough. For PB's basic style we should be more explicit;
      // && statement_number -3 == SM_ASSIGN_[TYPE]
      // && statement_number -1 == SM_INCR_[TYPE] || SM_ADDASSIGN_[TYPE]
      // && same variable in all cases
      ){
      init->type = for_init;
      jmp->type = for_jump;
      step->type = for_step;
      if_test->type = for_test;
      if_test->classified_count++;
      step->classified_count++;

      // loop body
      struct scope *scope = insert_scope(disassembly, init, if_test->next, prior->prev, prior);
      assert(scope);
      scope->break_dest = if_test->branch;
      scope->continue_dest = prior;

      prior->type = jump_next;
      if_test->branch->classified_count++;
      return;
    }
  }

  if (if_test->type == jump_false
    && if_test->branch->start_offset > if_test->start_offset){
    // one line if test?
    unsigned i=statement_number+1;
    // TODO, only skip generated statements? (eg SM_JUMP_1 to SM_RETURN_0)
    while(i < disassembly->statement_count && disassembly->statements[i]->end_line_number == if_test->start_line_number)
      i++;
    if (i>statement_number+1 && if_test->branch == disassembly->statements[i]){
      struct scope *scope = insert_scope(disassembly, if_test, NULL, NULL, prior);
      assert(scope);
      if_test->type = if_then;
      if_test->branch->classified_count++;
      return;
    }
  }

  if(if_test->type == jump_true
    && if_test->branch->start_offset > if_test->start_offset
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

  // plain if test?
  if (if_test->type == jump_false
    && if_test->branch->start_offset > if_test->start_offset){

    struct statement *indent_end = prior;
    const char *end_label = endif_label;
    if_test->type = if_then;

    if (if_test->end->stack[0]->definition->id == SM_CATCH_EXCEPTION_0){
      if_test->type = exception_catch;
      end_label = NULL;
      if (if_test->branch->end->definition->id == SM_GOSUB_1)
	indent_end = if_test->branch->branch->prev;
    }

    struct scope *scope = insert_scope(disassembly, if_test, if_test->next, indent_end, indent_end);
    assert(scope);
    if_test->branch->classified_count++;
    scope->end_label = end_label;
    return;
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
	  end->type = exception_gosub;
	  end = end->next;
	}

	assert(end->end->definition->id == SM_POP_TRY_0);
	end->type = exception_end_try;

	assert(try_end->end->definition->id == SM_JUMP_1);
	assert(try_end->end->args[0] == end_offset);
	
	// indent the guarded code (should be some??) with a scope that covers the entire try block
	if (ptr != try_end){
	  struct scope *scope = insert_scope(disassembly, ptr, ptr->next, try_end, end);
	  assert(scope);
	}
	// scope for the finally block (if any)
	if (finally){
	  struct scope *scope = insert_scope(disassembly, finally, finally, end_finally, end_finally);
	  assert(scope);
	  scope->begin_label = finally_label;
	}

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
      case exception_gosub:
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
  }

  for (i=0; i<disassembly->statement_count; i++){
    struct statement *jmp = disassembly->statements[i];
    if (jmp->type == jump_true || jmp->type == jump_false)
      classify_if_then(disassembly, i);
    if (jmp->type == exception_gosub){
      jmp->type = generated;
      jmp->branch->classified_count++;
    }
  }

  // work backwards to simplify merging elseif's as we go.
  for (i=disassembly->statement_count;i>1;i--){
    struct statement *jmp = disassembly->statements[i - 1];
    if (jmp->type == jump_goto && jmp->branch){
      // try to identify else's and elseif's...
      struct scope *if_scope = jmp->scope;
      if (if_scope
      && jmp->start->args[0] > jmp->start_offset
      && if_scope->start->type == if_then
      && if_scope->end_label == endif_label
      && if_scope->indent_end == jmp){
	struct statement *nxt = jmp->next;

	if (nxt->type == if_then
	  && nxt->branch
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

	// still unclassified?
	if (jmp->type == jump_goto){
	  // TODO check if a continue could be a better choice based on which blank line the end if will be placed on

	  // empty else? special case, leave the end if where it is but don't indent the else
	  if (nxt == jmp->branch){
	    jmp->type = jump_else;
	    jmp->branch->classified_count ++;
	    if_scope->indent_end = jmp->prev;
	    continue;
	  }else{
	    // else?
	    // (the range doesn't include the else, or the insert would fail)
	    struct scope *scope = insert_scope(disassembly, nxt, nxt, jmp->branch->prev, jmp->branch->prev);
	    if (scope){
	      scope->end_label = endif_label;
	      jmp->type = jump_else;
	    }
	  }
	}

	// any of the above, shrink the if test scope to exclude the else and drop the end if label
	if (jmp->type != jump_goto){
	  jmp->branch->classified_count ++;
	  if_scope->indent_end = jmp->prev;
	  if_scope->end_label = NULL;
	  continue;
	}
      }

      // try to reclassify goto's as continue, exit
      {
	struct scope *scope = jmp->scope;
	struct statement *pop_try = jmp->prev;
	while(scope){
	  if (scope->start->type == exception_try){
	    if (pop_try->start->definition->id == SM_POP_TRY_0){
	      // skip exception scopes only if the previous instruction will pop out of it.
	      pop_try->type = generated;
	      pop_try = pop_try->prev;
	      scope = scope->parent;
	      continue;
	    }
	    break;
	  }else if(scope->continue_dest)
	    // skip any non-loop scopes (without an exit location)
	    break;
	  scope = scope->parent;
	}

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
	  statement->type=jump_true;
	  break;

	case SM_JUMPFALSE_1:
	  statement->type=jump_false;
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

  if (stack_ptr){
    fflush(stdout);
    WARNF("Stack pointer (%u) is not zero at the end of %s!", stack_ptr, script->name);
  }

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

      case EXT:{
	i = (int)(*(++tokens));
	assert(i < inst->definition->args);
	i = inst->args[i];
	struct class_group_private *group = (struct class_group_private *)disassembly->group;
	assert(i<group->ext_ref_count);
	fputs(group->ref_names[i], fd);
      }break;

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
	const char *value = get_table_resource(group, &script->body->resources, *(const uint32_t*)&inst->args[i]);
	if (value)
	  fputs(value, fd);
	else
	  fputs("**NULL**", fd);
      }break;

      case RES_STRING_CONST:{
	i=(int)(*(++tokens));
	assert(i+1 < inst->definition->args);
	uint32_t offset = *(const uint32_t*)&inst->args[i];
	const char *str = get_table_string(group, &script->body->resources, offset);
	assert(str);
	const char *quoted = quote_escape_string(group, str);
	assert(quoted);
	fputs(quoted, fd);
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
	// actual value might be random and meaningless... not really sure.
	if ((type & 0xC000) == 0x8000){
	  struct class_def_private *class_def = (struct class_def_private *)disassembly->class_def;
	  assert(id < class_def->imports.count);
	  fprintf(fd, "%s", class_def->imports.names[id]);
	  break;
	}
	fprintf(fd, "::%s_%u", get_type_name(group, type), id);
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

struct print_state{
  unsigned line;
  int indent;
  struct statement *statement;
  struct scope *scopes[64];
  unsigned scope_count;
};

static void fputeol(FILE *fd, struct print_state *state){
  fputc('\n', fd);
  state->line++;

  //assert(state->indent>=0);
  int i = state->indent;
  while(i-->0)
    fputs("\t", fd);
}

// recursive, so we can process the top parent scope first
static unsigned begin_scope(FILE *fd, struct print_state *state, struct scope *scope){
  if (!scope)
    return 0;

  unsigned i = begin_scope(fd, state, scope->parent);
  if (i<64)
    state->scopes[i++]=scope;

  if (scope->start == state->statement){
    if (scope->begin_label){
      if (state->line < state->statement->start_line_number)
	fputeol(fd, state);
      fprintf(fd, "%s;", scope->begin_label);
    }
  }

  if (scope->indent_start == state->statement)
    state->indent++;
  return i;
}

void dump_statements(FILE *fd, struct disassembly *disassembly){
  if (IFDEBUG(DISASSEMBLY))
    dump_script_resources(fd, disassembly->group, disassembly->script);
  unsigned i;
  struct print_state state = {.line=1, .indent=0};

  for (i=0;i<disassembly->statement_count;i++){
    struct statement *statement = state.statement = disassembly->statements[i];

    state.scope_count = begin_scope(fd, &state, statement->scope);

    if (statement->classified_count < statement->destination_count){
      fprintf(fd, "Offset_%u:", statement->start->offset);
      if (state.line < statement->start_line_number)
	fputeol(fd, &state);
    }

    if (IFDEBUG(DISASSEMBLY) || statement->type != generated){
      while (state.line < statement->start_line_number)
	fputeol(fd, &state);

      // special cases;
      switch(statement->type){
	case exception_try:	fputs("try;", fd); break;
	case exception_end_try:	fputs("end try;", fd); break;
	case jump_loop:		fputs("loop;", fd); break;
	case jump_next:		fputs("next;", fd); break;
	case jump_exit:		fputs("exit;", fd); break;
	case jump_continue:	fputs("continue;", fd); break;
	case jump_else:		fputs("else;", fd); break;
	case jump_elseif:	fputs("else", fd); break;
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
	case exception_catch:{
	  struct instruction *inst = statement->end->stack[0]->stack[0];
	  unsigned var = inst->args[0];
	  struct variable_definition *variable = disassembly->script->local_variables[var];
	  fprintf(fd, "catch (%s %s);", variable->type, variable->name);
	}break;
	case for_init:
	  // TODO for [var] = [init] to [end] [step [N]]
	  // for now, C style;
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

    struct scope *scope = statement->scope;
    while(scope){
      if (scope->indent_end == statement){
	state.indent--;
	if (state.indent < 0){
	  fflush(fd);
	  DEBUGF(DISASSEMBLY, "Negative indent???");
	}
      }
      if (scope->end == statement && scope->end_label){
	if (!statement->next || state.line < statement->next->start_line_number)
	  fputeol(fd, &state);
	// TODO if there's no room for an end of line here, we should have popped all indents first.... somehow...
	fprintf(fd, "%s;", scope->end_label);
      }
      scope = scope->parent;
    }
  }
}

void disassembly_free(struct disassembly *disassembly){
  pool_release(disassembly->pool);
}
