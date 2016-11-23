
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
  DEBUGF("Entry %s", entry->name);
}

int main(int argc, const char **argv){
  if (argc<2){
    DEBUGF("Usage %s \"filename\" [\"Object name\"]", argv[0]);
    return 0;
  }

  struct library *lib = lib_open(argv[1]);
  if (lib){
    DEBUGF("opened %s (%s, comment %s)", lib->filename, lib->unicode?"unicode":"ansi", lib->comment);
    DEBUGF("Enumerating...");
    lib_enumerate(lib, callback, NULL);
    if (argc>=3){
      DEBUGF("Finding...");
      struct lib_entry *entry = lib_find(lib, argv[2]);
      struct class_group *class_group = class_parse(entry);
      class_free(class_group);
    }
    DEBUGF("Closing...");
    lib_close(lib);
  }
  return 0;
}
