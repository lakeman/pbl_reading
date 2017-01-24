#include <assert.h>
#include <string.h>
#include "disassembly.h"
#include "pool_alloc.h"
#include "debug.h"


// Ugly macro tom-foolery to apply the same macro to each argument
#define NARGS_SEQ(_x, _0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,N,...) N
#define NARGS(...) NARGS_SEQ(0, ##__VA_ARGS__, 12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define CAT2(x, y) x ## y
#define CAT(x, y) CAT2(x, y)
#define APPLY_0(m) CAT(m, _0)
#define APPLY_1(m, x1) m(x1)
#define APPLY_2(m, x1, x2) m(x1), m(x2)
#define APPLY_3(m, x1, x2, x3) m(x1), m(x2), m(x3)
#define APPLY_4(m, x1, x2, x3, x4) m(x1), m(x2), m(x3), m(x4)
#define APPLY_5(m, x1, x2, x3, x4, x5) m(x1), m(x2), m(x3), m(x4), m(x5)
#define APPLY_6(m, x1, x2, x3, x4, x5, x6) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6)
#define APPLY_7(m, x1, x2, x3, x4, x5, x6, x7) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6), m(x7)
#define APPLY_8(m, x1, x2, x3, x4, x5, x6, x7, x8) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6), m(x7), m(x8)
#define APPLY_9(m, x1, x2, x3, x4, x5, x6, x7, x8, x9) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6), m(x7), m(x8), m(x9)
#define APPLY_10(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6), m(x7), m(x8), m(x9), m(x10)
#define APPLY_11(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6), m(x7), m(x8), m(x9), m(x10), m(x11)
#define APPLY_12(m, x1, x2, x3, x4, x5, x6, x7, x8, x9, x10, x11, x12) m(x1), m(x2), m(x3), m(x4), m(x5), m(x6), m(x7), m(x8), m(x9), m(x10), m(x11), m(x12)
#define APPLY(macro, ...) CAT(APPLY_, NARGS(__VA_ARGS__))(macro, ##__VA_ARGS__)

#define TOKEN(X) ((const char *)(X))
#define TOKEN_0 NULL


// since each opcode may be used in multiple versions of PB, with different op-codes

// 1) define our own unique id for each instruction type (see disassembly.h)

// 2) a global table with metadata for parsing instructions of that type
#define __DEFINE(NAME,ARGS,STACK_KIND,STACK_ARG,PRECEDENCE,OPERATION,...) \
  struct pcode_def OP_##NAME##_##ARGS = {\
    .id=NAME##_##ARGS, \
    .name=#NAME, \
    .args=ARGS,\
    .precedence=PRECEDENCE,\
    .operation=OPERATION,\
    .stack_kind=STACK_KIND,\
    .stack_arg=STACK_ARG,\
    .tokens={APPLY(TOKEN, ##__VA_ARGS__), NULL}};

#define BEFORE(X) 1
#define AFTER(X) 1

#include "opcodes.inc"

#undef __DEFINE
#undef BEFORE
#undef AFTER


// 3) declare which pcode instruction numbers relate to which operations for each version of PB


#define BEFORE(X) (PBVERSION < X)
#define AFTER(X) (PBVERSION >= X)
#define __DEFINE(NAME,ARGS,...) & OP_##NAME##_##ARGS ,

#define PBVERSION PB50
struct pcode_def *PB50_opcodes[] = {
#include "opcodes.inc"
};
#undef PBVERSION

#define PBVERSION PB80
struct pcode_def *PB80_opcodes[] = {
#include "opcodes.inc"
};
#undef PBVERSION

#define PBVERSION PB90
struct pcode_def *PB90_opcodes[] = {
#include "opcodes.inc"
};
#undef PBVERSION

#define PBVERSION PB100
struct pcode_def *PB100_opcodes[] = {
#include "opcodes.inc"
};
#undef PBVERSION

#define PBVERSION PB105
struct pcode_def *PB105_opcodes[] = {
#include "opcodes.inc"
};
#undef PBVERSION

#define PBVERSION PB120
struct pcode_def *PB120_opcodes[] = {
#include "opcodes.inc"
};
#undef PBVERSION

#define NELS(A) (sizeof (A) / sizeof *(A))

unsigned PB50_maxcode = NELS(PB50_opcodes);
unsigned PB80_maxcode = NELS(PB80_opcodes);
unsigned PB90_maxcode = NELS(PB90_opcodes);
unsigned PB100_maxcode = NELS(PB100_opcodes);
unsigned PB105_maxcode = NELS(PB105_opcodes);
unsigned PB120_maxcode = NELS(PB120_opcodes);

