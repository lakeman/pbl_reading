#include "output.h"
#include "class.h"

static void write_variable(FILE *fd, struct variable_definition *variable){
  if (variable->read_access || variable->write_access){
    if (variable->read_access == variable->write_access){
      fprintf(fd, "%s ", variable->read_access);
    }else{
      if (variable->read_access)
	fprintf(fd, "%sread ", variable->read_access);
      if (variable->read_access)
	fprintf(fd, "%swrite ", variable->write_access);
    }
  }
  if (variable->constant)
    fprintf(fd, "constant ");
  fprintf(fd, "%s %s", variable->type, variable->name);
  if (variable->dimensions)
    fprintf(fd, "%s", variable->dimensions);
  fprintf(fd, "\n");
}

static void write_variables(FILE *fd, int user_defined, const char *type, struct variable_definition *variable){
  if (!variable)
    return;
  int found = 0;
  while(variable){
    if (variable->user_defined == user_defined){
      if (!found){
	found = 1;
	if (type)
	  fprintf(fd, "%s variables\n", type);
      }
      write_variable(fd, variable);
    }
    variable = variable->next;
  }

  if (found && type)
    fprintf(fd, "end variables\n\n");
}

static void write_type_dec(FILE *fd, struct class_definition *class_def){
  // global?

  fprintf(fd, "type %s from %s", class_def->name, class_def->ancestor);
  if (class_def->parent)
    fprintf(fd, " within %s", class_def->parent);
  if (class_def->autoinstantiate)
    fprintf(fd, " autoinstantiate");
  fprintf(fd, "\n");
}

static void write_forward(FILE *fd, struct class_group *group){
  struct class_definition *class_def = group->classes;
  fprintf(fd, "forward\n");
  // TODO this probably isn't quite right for nested classes, eg menu's
  while(class_def){
    write_type_dec(fd, class_def);
    fprintf(fd, "end type\n");
    class_def = class_def->next;
  }
  write_variables(fd, 0, NULL, group->global_variables);
  fprintf(fd, "end forward\n\n");
}

static void write_method_header(FILE *fd, struct script_definition *script){
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
  struct argument_definition *arg = script->arguments;
  while(arg){
    if (arg!=script->arguments)
      fprintf(fd, ", ");
    if (arg->access)
      fprintf(fd, "%s ", arg->access);
    fprintf(fd, "%s", arg->type);
    if (arg->name)
      fprintf(fd, " %s", arg->name);
    if (arg->dimensions)
      fprintf(fd, "%s", arg->dimensions);
    arg = arg->next;
  }
  fprintf(fd,")");
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
  struct script_definition *script = class_def->scripts;
  int found=0;
  
  while(script){
    if (!script->event && external == (script->library ? 1:0)){
      if (!found){
	fprintf(fd, "%s prototypes\n", external?"type":"forward");
	found = 1;
      }
      write_method_header(fd, script);
      fprintf(fd,"\n");
    }
    script=script->next;
  }
  if (found)
    fprintf(fd, "end prototypes\n\n");
}

static void write_class(FILE *fd, struct class_definition *class_def){
  write_type_dec(fd, class_def);
  write_variables(fd, 0, NULL, class_def->instance_variables);

  struct script_definition *script = class_def->scripts;
  while(script){
    if (script->event){
      write_method_header(fd, script);
      fprintf(fd,"\n");
    }
    script=script->next;
  }

  fprintf(fd, "end type\n\n");

  // TODO global variable goes here?
  write_prototypes(fd, 1, class_def);
  write_variables(fd, 1, "type", class_def->instance_variables);
  write_prototypes(fd, 0, class_def);

  script = class_def->scripts;
  while(script){
    if (script->implemented){
      write_method_header(fd, script);
      fprintf(fd, ";");
      struct variable_definition *variable = script->local_variables;
      // skip arguments
      unsigned i;
      for(i=0;variable && i<script->argument_count;i++)
	variable = variable->next;
      write_variables(fd, 0, NULL, variable);
      if (script->event)
	fprintf(fd, "\nend event\n\n");
      else if(script->return_type)
	fprintf(fd, "\nend function\n\n");
      else
	fprintf(fd, "\nend subroutine\n\n");
    }
    script=script->next;
  }

}

void write_group(FILE *fd, struct class_group *group){
  write_forward(fd, group);
  // TODO structures
  write_variables(fd, 1, "shared", group->global_variables);
  // this works, but it's not exactly right...
  write_variables(fd, 0, "global", group->global_variables);

  struct class_definition *class_def = group->classes;
  while(class_def){
    write_class(fd, class_def);
    class_def = class_def->next;
  }
}
