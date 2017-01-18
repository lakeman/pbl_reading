#include "output.h"
#include "class.h"
#include "debug.h"
#include "disassembly.h"

static void write_variable(FILE *fd, struct variable_definition *variable){
  if (variable->read_access || variable->write_access){
    if (variable->read_access == variable->write_access){
      fprintf(fd, "%s ", variable->read_access);
    }else{
      if (variable->read_access)
	fprintf(fd, "%sread ", variable->read_access);
      if (variable->write_access)
	fprintf(fd, "%swrite ", variable->write_access);
    }
  }
  if (variable->constant)
    fprintf(fd, "constant ");
  if (variable->indirect)
    fprintf(fd, "indirect ");
  fprintf(fd, "%s %s", variable->type, variable->name);
  if (variable->dimensions)
    fprintf(fd, "%s", variable->dimensions);

  if (variable->value_count && variable->initial_values){
    fputs(" = ", fd);
    if (variable->indirect || variable->dimensions || variable->value_count>1)
      fputs("{", fd);

    unsigned i;
    for(i=0;i<variable->value_count;i++){
      if (i>0)
	fputs(", ", fd);
      if (variable->initial_values[i])
	fputs(variable->initial_values[i], fd);
    }

    if (variable->indirect || variable->dimensions || variable->value_count>1)
      fputs("}", fd);
  }
  fprintf(fd, "\n");
}

static void write_variables(FILE *fd, int user_defined, const char *type, struct variable_definition *variable[]){
  if (!variable)
    return;
  int found = 0;
  unsigned i;
  for (i=0;variable[i];i++){
    if (variable[i]->user_defined == user_defined){
      if (!found){
	found = 1;
	if (type)
	  fprintf(fd, "%s variables\n", type);
      }
      write_variable(fd, variable[i]);
    }
  }

  if (found && type)
    fprintf(fd, "end variables\n\n");
}

static void write_type_dec(FILE *fd, const char *name, struct class_definition *class_def){
  // global?
  fprintf(fd, "type %s from %s", name, class_def->ancestor);
  if (class_def->parent)
    fprintf(fd, " within %s", class_def->parent);
  if (class_def->autoinstantiate)
    fprintf(fd, " autoinstantiate");
  fprintf(fd, "\n");
}

static void write_forward(FILE *fd, struct class_group *group){
  fprintf(fd, "forward\n");
  // TODO this probably isn't quite right for nested classes, eg menu's
  unsigned i;
  for (i=0;i<group->type_count;i++){
    if (group->types[i].type == class_type){
      write_type_dec(fd, group->types[i].name, group->types[i].class_definition);
      fprintf(fd, "end type\n");
    }
  }
  write_variables(fd, 0, NULL, group->global_variables);
  fprintf(fd, "end forward\n\n");
}

static void write_method_header(FILE *fd, struct script_definition *script){
  if (script->hidden){
    // not sure what the right syntax is for this undocumented flag
    fprintf(fd, "/* HIDDEN! */ ");
  }
  if (script->event_type){
    fprintf(fd, "event %s %s", script->name+1, script->event_type);
    return;
  }else if(script->event){
    if (script->return_type)
      fprintf(fd, "event type %s %s(", script->return_type, script->name);
    else
      fprintf(fd, "event %s(", script->name);
  } else if (script->return_type){
    fprintf(fd, "%s function %s %s(",
      script->access?script->access:"public",
      script->return_type,
      script->name);
  }else{
    fprintf(fd, "%s subroutine %s(",
      script->access?script->access:"public",
      script->name);
  }
  unsigned i;
  for(i=0; i<script->argument_count; i++){
    if (i>0)
      fprintf(fd, ", ");
    struct argument_definition *arg = script->arguments[i];
    if (arg->access)
      fprintf(fd, "%s ", arg->access);
    fprintf(fd, "%s", arg->type);
    if (arg->name)
      fprintf(fd, " %s", arg->name);
    if (arg->dimensions)
      fprintf(fd, "%s", arg->dimensions);
  }
  fprintf(fd,")");
  for (i=0; i<script->throws_count; i++){
    if (i==0)
      fprintf(fd, " throws %s", script->throws[i]);
    else
      fprintf(fd, ", %s", script->throws[i]);
  }
  if (script->rpc)
    fprintf(fd, " rpcfunc");
  if (script->library){
    if (script->system)
      fprintf(fd, " system");
    fprintf(fd, " library \"%s\"", script->library);
  }
  if (script->external_name)
    fprintf(fd, " alias for \"%s\"", script->external_name);
}

static void write_prototypes(FILE *fd, int external, struct class_definition *class_def){
  int found=0;
  unsigned i;
  for (i=0;class_def->scripts[i];i++){
    struct script_definition *script = class_def->scripts[i];
    if (!script->event && external == (script->library ? 1:0)){
      if (!found){
	fprintf(fd, "%s prototypes\n", external?"type":"forward");
	found = 1;
      }
      write_method_header(fd, script);
      fprintf(fd,"\n");
    }
  }
  if (found)
    fprintf(fd, "end prototypes\n\n");
}

static void write_script_body(FILE *fd, struct class_group *group, struct class_definition *class_def, struct script_definition *script){
  write_method_header(fd, script);
  fprintf(fd, ";");
  // variable declarations, skipping arguments
  write_variables(fd, 0, NULL, script->local_variables + script->argument_count);
  struct disassembly *code = disassemble(group, class_def, script);
  if (code){

    dump_statements(fd, code);

    //fprintf(fd, "/* Resource table;\n");
    //dump_script_resources(fd, group, script);
    //fprintf(fd, "\nInstructions;\n");
    //dump_pcode(fd, code);
    //fprintf(fd, "*/\n");
    disassembly_free(code);
  }
  if (script->event)
    fprintf(fd, "\nend event\n\n");
  else if(script->return_type)
    fprintf(fd, "\nend function\n\n");
  else
    fprintf(fd, "\nend subroutine\n\n");
}

static void write_class(FILE *fd, struct class_group *UNUSED(group), const char *name, struct class_definition *class_def){
  write_type_dec(fd, name, class_def);
  write_variables(fd, 0, NULL, class_def->instance_variables);

  unsigned i;
  for (i=0;class_def->scripts[i];i++){
    if (class_def->scripts[i]->event && !class_def->scripts[i]->in_ancestor){
      write_method_header(fd, class_def->scripts[i]);
      fprintf(fd,"\n");
    }
  }

  fprintf(fd, "end type\n\n");

  // TODO global variable goes here?
  write_prototypes(fd, 1, class_def);
  write_variables(fd, 1, "type", class_def->instance_variables);
  write_prototypes(fd, 0, class_def);

  // first the functions
  for (i=0;class_def->scripts[i];i++){
    struct script_definition *script = class_def->scripts[i];
    if (script->implemented && !script->event){
      write_script_body(fd, group, class_def, script);
    }
  }

  // then the events
  for (i=0;class_def->scripts[i];i++){
    struct script_definition *script = class_def->scripts[i];
    if (script->implemented && script->event){
      write_script_body(fd, group, class_def,script);
    }
  }
}

void write_group(FILE *fd, struct class_group *group){
  write_forward(fd, group);
  // TODO structure definitions first, not last
  write_variables(fd, 1, "shared", group->global_variables);
  // this works, but it's not exactly right...
  write_variables(fd, 0, "global", group->global_variables);

  unsigned i;
  for (i=0;i<group->type_count;i++){
    if (group->types[i].type == class_type)
      write_class(fd, group, group->types[i].name, group->types[i].class_definition);
  }
}
