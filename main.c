
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "lib.h"
#include "debug.h"

void __assert_fail(const char * assertion, const char * file, unsigned int line, const char * function) {
    fprintf(stderr, "Assert: (%s) failed at %s:%d in function %s\n", assertion, file, line, function);
    exit(-1);
}

void callback(struct lib_entry *entry, void *UNUSED(context)){
  DEBUGF("Entry %s", entry->name);
}

int main(int argc, const char **argv){
  assert(argc>=1);
  struct library *lib = lib_open(argv[1]);
  if (lib){
    DEBUGF("opened %s (%s, comment %s)", lib->filename, lib->unicode?"unicode":"ansi", lib->comment);
    lib_enumerate(lib, callback, NULL);
    lib_find(lib, argv[2]);
    lib_close(lib);
  }
  return 0;
}
