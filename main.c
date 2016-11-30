
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "lib.h"
#include "debug.h"
#include "class.h"

void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function) {
    fprintf(stderr, "Assert: (%s) failed at %s:%d in function %s\n", assertion, file, line, function);
    exit(-1);
}

static void callback(struct lib_entry *entry, void *UNUSED(context)){
  printf("Entry %s\n", entry->name);
}

static void dump_variables(struct variable_definition *variable){
  while(variable){
    if (variable->read_access == variable->write_access){
      printf("      %s%s%s %s%s\n",
	variable->read_access,
	(*variable->read_access)?" ":"",
	variable->type,
	variable->name,
	variable->dimensions);
    }else{
      printf("      %s%s%s%s%s %s%s\n",
	variable->read_access,
	(*variable->read_access)?"read ":"",
	variable->write_access,
	(*variable->write_access)?"write ":"",
	variable->type,
	variable->name,
	variable->dimensions);
    }
    variable = variable->next;
  }
}

int main(int argc, const char **argv){
  if (argc<2){
    fprintf(stderr, "Usage %s \"filename\" [\"Object name\"]\n", argv[0]);
    return 0;
  }

  struct library *lib = lib_open(argv[1]);
  if (lib){
    printf("opened %s (%s, comment %s)\n", lib->filename, lib->unicode?"unicode":"ansi", lib->comment);
    if (argc>=3){
      printf("Finding %s...\n", argv[2]);
      struct lib_entry *entry = lib_find(lib, argv[2]);
      if (entry){
	struct class_group *class_group = class_parse(entry);
	if (class_group->global_variables){
	  printf("- Global variables;\n");
	  dump_variables(class_group->global_variables);
	}
	struct class_definition *class_def = class_group->classes;
	while(class_def){
	  printf("- Class %s\n", class_def->name);
	  if (class_def->instance_variables){
	    printf("  - Instance variables;\n");
	    dump_variables(class_def->instance_variables);
	  }
	  struct script_definition *script = class_def->scripts;
	  while(script){
	    printf("  - Script %s(", script->name);
	    struct argument_definition *arg = script->arguments;
	    while(arg){
	      if (arg!=script->arguments)
		printf(", ");
	      printf("%s%s%s %s%s",
		arg->access?arg->access:"",
		arg->access?" ":"",
		arg->type,
		arg->name?arg->name:"",
		arg->dimensions?arg->dimensions:"");
	      arg = arg->next;
	    }
	    printf(")\n");
	    if (script->local_variables){
	      printf("    - Local variables;\n");
	      dump_variables(script->local_variables);
	    }
	    script = script->next;
	  }
	  class_def = class_def->next;
	}
	class_free(class_group);
      }else{
	printf("Not found?\n");
      }
    }else{
      printf("Enumerating %s...\n", argv[1]);
      lib_enumerate(lib, callback, NULL);
    }
    printf("Closing...\n");
    lib_close(lib);
  }
  return 0;
}
