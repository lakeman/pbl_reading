
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

void callback(struct lib_entry *entry, void *UNUSED(context)){
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
    printf("Enumerating...\n");
    lib_enumerate(lib, callback, NULL);
    if (argc>=3){
      printf("Finding...\n");
      struct lib_entry *entry = lib_find(lib, argv[2]);
      struct class_group *class_group = class_parse(entry);
      class_free(class_group);
    }
    printf("Closing...\n");
    lib_close(lib);
  }
  return 0;
}
