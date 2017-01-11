
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "lib.h"
#include "debug.h"
#include "class.h"
#include "output.h"

void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function) {
  fflush(stdout);
  fprintf(stderr, "Assert: (%s) failed at %s:%d in function %s\n", assertion, file, line, function);
  exit(-1);
}

static void callback(struct lib_entry *entry, void *UNUSED(context)){
  printf("Entry %s\n", entry->name);
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
	write_group(stdout, class_group);
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
