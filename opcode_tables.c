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

#include "opcodes.inc"

#undef __DEFINE


// 3) declare which pcode instruction numbers relate to which operations for each version of PB
#define OP(NAME,ARGS) & OP_##NAME##_##ARGS

struct pcode_def *PB120_opcodes[] = {
// TODO, use #defs in a common file to define these for each version?
OP(SM_RETURN,0),
OP(SM_STORE_RETURN_VAL,1),
OP(SM_JUMPTRUE,1),
OP(SM_JUMPFALSE,1),
OP(SM_JUMP,1),
OP(SM_DBSTART,0),
OP(SM_DBCOMMIT,0),
OP(SM_DBROLLBACK,0),
OP(SM_DBSTOP,0),
OP(SM_DBCLOSE,0),
OP(SM_DBOPEN,1),
OP(SM_DBDELETE,3),
OP(SM_DBUPDATE,3),
OP(SM_DBEXECUTE,1),
OP(SM_DBFETCH,3),
OP(SM_DBINSERT,3),
OP(SM_DBSELECT,4),
OP(SM_DESTROY,0),
OP(SM_HALT,1),
OP(SM_EVENTCALL,5),
OP(SM_LVALUE_EXPR,0),
OP(SM_DBEXECUTEDYN,3),
OP(SM_DBPREPARE,0),
OP(SM_DBOPENDYN,3),
OP(SM_DBEXECDYNPROC,3),
OP(SM_DBDESCRIBE,0),
OP(SM_DBSELECTBLOB,4),
OP(SM_DBUPDATEBLOB,3),
OP(SM_DBSELECTCLOB,5),
OP(SM_DBUPDATECLOB,4),
OP(SM_PUSH_LOCAL_VAR,1),
OP(SM_PUSH_SHARED_VAR,1),
OP(SM_PUSH_CONST_REF,2),
OP(SM_PUSH_THIS,0),
OP(SM_PUSH_PARENT,0),
OP(SM_PUSH_PRIMARY,0),
OP(SM_AND_BOOL,0),
OP(SM_OR_BOOL,0),
OP(SM_NOT_BOOL,0),
OP(SM_DOT,1),
OP(SM_INDEX,0),
OP(SM_GLOBFUNCCALL,3),
OP(SM_SYSFUNCCALL,2),
OP(SM_DLLFUNCCALL,3),
OP(SM_DOTFUNCCALL,4),
OP(SM_CREATE,2),
OP(SM_ARRAYLIST,3),
OP(SM_PUSH_LOCAL_GLOBREF,1),
OP(SM_PUSH_LOCAL_ARGREF,1),
OP(SM_PUSH_SHARED_GLOBREF,1),
OP(SM_PUSH_CONST_INT,1),
OP(SM_PUSH_CONST_UINT,1),
OP(SM_PUSH_CONST_LONG,2),
OP(SM_PUSH_CONST_ULONG,2),
OP(SM_PUSH_CONST_DEC,2),
OP(SM_PUSH_CONST_FLOAT,2),
OP(SM_PUSH_CONST_DOUBLE,2),
OP(SM_PUSH_CONST_TIME,2),
OP(SM_PUSH_CONST_DATE,2),
OP(SM_PUSH_CONST_STRING,2),
OP(SM_PUSH_CONST_BOOL,1),
OP(SM_PUSH_CONST_ENUM,2),
OP(SM_CNV_INT_TO_UINT,1),
OP(SM_CNV_INT_TO_LONG,1),
OP(SM_CNV_INT_TO_ULONG,1),
OP(SM_CNV_INT_TO_DEC,1),
OP(SM_CNV_INT_TO_FLOAT,1),
OP(SM_CNV_INT_TO_DOUBLE,1),
OP(SM_CNV_UINT_TO_LONG,1),
OP(SM_CNV_UINT_TO_ULONG,1),
OP(SM_CNV_UINT_TO_DEC,1),
OP(SM_CNV_UINT_TO_FLOAT,1),
OP(SM_CNV_UINT_TO_DOUBLE,1),
OP(SM_CNV_LONG_TO_ULONG,1),
OP(SM_CNV_LONG_TO_DEC,1),
OP(SM_CNV_LONG_TO_FLOAT,1),
OP(SM_CNV_LONG_TO_DOUBLE,1),
OP(SM_CNV_ULONG_TO_DEC,1),
OP(SM_CNV_ULONG_TO_FLOAT,1),
OP(SM_CNV_ULONG_TO_DOUBLE,1),
OP(SM_CNV_DEC_TO_FLOAT,1),
OP(SM_CNV_DEC_TO_DOUBLE,1),
OP(SM_CNV_FLOAT_TO_DOUBLE,1),
OP(SM_ADD_INT,0),
OP(SM_ADD_UINT,0),
OP(SM_ADD_LONG,0),
OP(SM_ADD_ULONG,0),
OP(SM_ADD_DEC,0),
OP(SM_ADD_FLOAT,0),
OP(SM_ADD_DOUBLE,0),
OP(SM_SUB_INT,0),
OP(SM_SUB_UINT,0),
OP(SM_SUB_LONG,0),
OP(SM_SUB_ULONG,0),
OP(SM_SUB_DEC,0),
OP(SM_SUB_FLOAT,0),
OP(SM_SUB_DOUBLE,0),
OP(SM_MULT_INT,0),
OP(SM_MULT_UINT,0),
OP(SM_MULT_LONG,0),
OP(SM_MULT_ULONG,0),
OP(SM_MULT_DEC,0),
OP(SM_MULT_FLOAT,0),
OP(SM_MULT_DOUBLE,0),
OP(SM_DIV_INT,0),
OP(SM_DIV_UINT,0),
OP(SM_DIV_LONG,0),
OP(SM_DIV_ULONG,0),
OP(SM_DIV_DEC,0),
OP(SM_DIV_FLOAT,0),
OP(SM_DIV_DOUBLE,0),
OP(SM_POWER_INT,0),
OP(SM_POWER_UINT,0),
OP(SM_POWER_LONG,0),
OP(SM_POWER_ULONG,0),
OP(SM_POWER_DEC,0),
OP(SM_POWER_FLOAT,0),
OP(SM_POWER_DOUBLE,0),
OP(SM_NEGATE_INT,0),
OP(SM_NEGATE_UINT,0),
OP(SM_NEGATE_LONG,0),
OP(SM_NEGATE_ULONG,0),
OP(SM_NEGATE_DEC,0),
OP(SM_NEGATE_FLOAT,0),
OP(SM_NEGATE_DOUBLE,0),
OP(SM_CAT_STRING,0),
OP(SM_CAT_BINARY,0),
OP(SM_ASSIGN_ARRAY,1),
OP(SM_ASSIGN_INT,1),
OP(SM_ASSIGN_UINT,1),
OP(SM_ASSIGN_LONG,1),
OP(SM_ASSIGN_ULONG,1),
OP(SM_ASSIGN_DEC,1),
OP(SM_ASSIGN_FLOAT,1),
OP(SM_ASSIGN_DOUBLE,1),
OP(SM_ASSIGN_BLOB,1),
OP(SM_ASSIGN_STRING,1),
OP(SM_ASSIGN_TIME,1),
OP(SM_ASSIGN_OBINST,1),
OP(SM_ASSIGN_ANCESTOR,1),
OP(SM_ASSIGN_ENUM,1),
OP(SM_CNV_UINT_TO_INT,1),
OP(SM_CNV_LONG_TO_INT,1),
OP(SM_CNV_ULONG_TO_INT,1),
OP(SM_CNV_DEC_TO_INT,1),
OP(SM_CNV_FLOAT_TO_INT,1),
OP(SM_CNV_DOUBLE_TO_INT,1),
OP(SM_CNV_LONG_TO_UINT,1),
OP(SM_CNV_ULONG_TO_UINT,1),
OP(SM_CNV_DEC_TO_UINT,1),
OP(SM_CNV_FLOAT_TO_UINT,1),
OP(SM_CNV_DOUBLE_TO_UINT,1),
OP(SM_CNV_ULONG_TO_LONG,1),
OP(SM_CNV_DEC_TO_LONG,1),
OP(SM_CNV_FLOAT_TO_LONG,1),
OP(SM_CNV_DOUBLE_TO_LONG,1),
OP(SM_CNV_DEC_TO_ULONG,1),
OP(SM_CNV_FLOAT_TO_ULONG,1),
OP(SM_CNV_DOUBLE_TO_ULONG,1),
OP(SM_CNV_FLOAT_TO_DEC,1),
OP(SM_CNV_DOUBLE_TO_DEC,1),
OP(SM_CNV_DOUBLE_TO_FLOAT,1),
OP(SM_CNV_STRING_TO_CHAR,2),
OP(SM_CNV_CHAR_TO_STRING,1),
OP(SM_CNV_STRING_TO_CHARARRAY,2),
OP(SM_CNV_CHARARRAY_TO_STRING,1),
OP(SM_EQ_INT,0),
OP(SM_EQ_UINT,0),
OP(SM_EQ_LONG,0),
OP(SM_EQ_ULONG,0),
OP(SM_EQ_DEC,0),
OP(SM_EQ_FLOAT,0),
OP(SM_EQ_DOUBLE,0),
OP(SM_EQ_STRING,2),
OP(SM_EQ_BOOL,0),
OP(SM_EQ_BINARY,2),
OP(SM_EQ_TIME,2),
OP(SM_EQ_DATE,2),
OP(SM_EQ_DATETIME,2),
OP(SM_EQ_CHAR,0),
OP(SM_EQ_OBINST,0),
OP(SM_EQ_ENUM,0),
OP(SM_NE_INT,0),
OP(SM_NE_UINT,0),
OP(SM_NE_LONG,0),
OP(SM_NE_ULONG,0),
OP(SM_NE_DEC,0),
OP(SM_NE_FLOAT,0),
OP(SM_NE_DOUBLE,0),
OP(SM_NE_STRING,2),
OP(SM_NE_BOOL,0),
OP(SM_NE_BINARY,2),
OP(SM_NE_TIME,2),
OP(SM_NE_DATE,2),
OP(SM_NE_DATETIME,2),
OP(SM_NE_CHAR,0),
OP(SM_NE_OBINST,0),
OP(SM_NE_ENUM,0),
OP(SM_GT_INT,0),
OP(SM_GT_UINT,0),
OP(SM_GT_LONG,0),
OP(SM_GT_ULONG,0),
OP(SM_GT_DEC,0),
OP(SM_GT_FLOAT,0),
OP(SM_GT_DOUBLE,0),
OP(SM_GT_STRING,2),
OP(SM_GT_TIME,2),
OP(SM_GT_DATE,2),
OP(SM_GT_DATETIME,2),
OP(SM_GT_CHAR,0),
OP(SM_LT_INT,0),
OP(SM_LT_UINT,0),
OP(SM_LT_LONG,0),
OP(SM_LT_ULONG,0),
OP(SM_LT_DEC,0),
OP(SM_LT_FLOAT,0),
OP(SM_LT_DOUBLE,0),
OP(SM_LT_STRING,2),
OP(SM_LT_TIME,2),
OP(SM_LT_DATE,2),
OP(SM_LT_DATETIME,2),
OP(SM_LT_CHAR,0),
OP(SM_GE_INT,0),
OP(SM_GE_UINT,0),
OP(SM_GE_LONG,0),
OP(SM_GE_ULONG,0),
OP(SM_GE_DEC,0),
OP(SM_GE_FLOAT,0),
OP(SM_GE_DOUBLE,0),
OP(SM_GE_STRING,2),
OP(SM_GE_TIME,2),
OP(SM_GE_DATE,2),
OP(SM_GE_DATETIME,2),
OP(SM_GE_CHAR,0),
OP(SM_LE_INT,0),
OP(SM_LE_UINT,0),
OP(SM_LE_LONG,0),
OP(SM_LE_ULONG,0),
OP(SM_LE_DEC,0),
OP(SM_LE_FLOAT,0),
OP(SM_LE_DOUBLE,0),
OP(SM_LE_STRING,2),
OP(SM_LE_TIME,2),
OP(SM_LE_DATE,2),
OP(SM_LE_DATETIME,2),
OP(SM_LE_CHAR,0),
OP(SM_INCR_INT,2),
OP(SM_INCR_UINT,2),
OP(SM_INCR_LONG,2),
OP(SM_INCR_ULONG,2),
OP(SM_INCR_DEC,2),
OP(SM_INCR_FLOAT,2),
OP(SM_INCR_DOUBLE,2),
OP(SM_DECR_INT,2),
OP(SM_DECR_UINT,2),
OP(SM_DECR_LONG,2),
OP(SM_DECR_ULONG,2),
OP(SM_DECR_DEC,2),
OP(SM_DECR_FLOAT,2),
OP(SM_DECR_DOUBLE,2),
OP(SM_ADDASSIGN_INT,2),
OP(SM_ADDASSIGN_UINT,2),
OP(SM_ADDASSIGN_LONG,2),
OP(SM_ADDASSIGN_ULONG,2),
OP(SM_ADDASSIGN_DEC,2),
OP(SM_ADDASSIGN_FLOAT,2),
OP(SM_ADDASSIGN_DOUBLE,2),
OP(SM_SUBASSIGN_INT,2),
OP(SM_SUBASSIGN_UINT,2),
OP(SM_SUBASSIGN_LONG,2),
OP(SM_SUBASSIGN_ULONG,2),
OP(SM_SUBASSIGN_DEC,2),
OP(SM_SUBASSIGN_FLOAT,2),
OP(SM_SUBASSIGN_DOUBLE,2),
OP(SM_MULTASSIGN_INT,2),
OP(SM_MULTASSIGN_UINT,2),
OP(SM_MULTASSIGN_LONG,2),
OP(SM_MULTASSIGN_ULONG,2),
OP(SM_MULTASSIGN_DEC,2),
OP(SM_MULTASSIGN_FLOAT,2),
OP(SM_MULTASSIGN_DOUBLE,2),
OP(SM_DUP_STACKED_LVALUE,1),
OP(SM_EQ_ARRAY,0),
OP(SM_NE_ARRAY,0),
OP(SM_CONV_TO_LVALUE,0),
OP(SM_PUSH_LOCAL_VAR_LV,1),
OP(SM_PUSH_SHARED_VAR_LV,1),
OP(SM_PUSH_LOCAL_GLOBREF_LV,1),
OP(SM_PUSH_LOCAL_ARGREF_LV,1),
OP(SM_PUSH_SHARED_GLOBREF_LV,1),
OP(SM_DOT_LV,1),
OP(SM_INDEX_LV,0),
OP(SM_NOOP,0),
OP(SM_POP,0),
OP(SM_FREE,0),
OP(SM_PUSH_RESULT,0),
OP(SM_POP_POP,0),
OP(SM_POP_FREE,0),
OP(SM_FREE_POP,0),
OP(SM_FREE_FREE,0),
OP(SM_COPY_ARRAY_INSTANCE,1),
OP(SM_COPY_STRUCTURE_INSTANCE,1),
OP(SM_COPY_CONST_DOUBLE,1),
OP(SM_COPY_CONST_DEC,1),
OP(SM_COPY_CONST_DATE,1),
OP(SM_COPY_CONST_TIME,1),
OP(SM_COPY_CONST_DATETIME,1),
OP(SM_COPY_CONST_STRING,1),
OP(SM_COPY_LVALUE_DOUBLE,1),
OP(SM_COPY_LVALUE_DEC,1),
OP(SM_COPY_LVALUE_DATE,1),
OP(SM_COPY_LVALUE_TIME,1),
OP(SM_COPY_LVALUE_DATETIME,1),
OP(SM_COPY_LVALUE_STRING,1),
OP(SM_COPY_LVALUE_BINARY,1),
OP(SM_POP_N_TIMES,1),
OP(SM_FREE_NODE_N,1),
OP(SM_CONV_DBL_RVALUE_TO_PTR,1),
OP(SM_COPY_EXPR_DOUBLE,1),
OP(SM_BREAKPOINT,0),
OP(SM_INDEX_ERR_CHK,0),
OP(SM_DOT_DOUBLE,1),
OP(SM_DOT_DEC,1),
OP(SM_INDEX_DOUBLE,0),
OP(SM_INDEX_DEC,0),
OP(SM_INDEX_ERR_CHK_DBL,0),
OP(SM_INDEX_ERR_CHK_DEC,0),
OP(SM_GLOBFUNCCALL_DOUBLE,3),
OP(SM_GLOBFUNCCALL_DEC,3),
OP(SM_SYSFUNCCALL_DOUBLE,2),
OP(SM_SYSFUNCCALL_DEC,2),
OP(SM_DLLFUNCCALL_DOUBLE,3),
OP(SM_DLLFUNCCALL_DEC,3),
OP(SM_DOTFUNCCALL_DOUBLE,4),
OP(SM_DOTFUNCCALL_DEC,4),
OP(SM_PUSH_LOCAL_VAR_DOUBLE,1),
OP(SM_PUSH_LOCAL_VAR_DEC,1),
OP(SM_PUSH_SHARED_VAR_DOUBLE,1),
OP(SM_PUSH_SHARED_VAR_DEC,1),
OP(SM_PUSH_LOCAL_GLOBREF_DOUBLE,1),
OP(SM_PUSH_LOCAL_GLOBREF_DEC,1),
OP(SM_PUSH_LOCAL_ARGREF_DOUBLE,1),
OP(SM_PUSH_LOCAL_ARGREF_DEC,1),
OP(SM_PUSH_SHARED_GLOBREF_DOUBLE,1),
OP(SM_PUSH_SHARED_GLOBREF_DEC,1),
OP(SM_ASSIGN_ANY,2),
OP(SM_CNV_ANY_TO_INT,1),
OP(SM_CNV_ANY_TO_UINT,1),
OP(SM_CNV_ANY_TO_LONG,1),
OP(SM_CNV_ANY_TO_ULONG,1),
OP(SM_CNV_ANY_TO_DEC,1),
OP(SM_CNV_ANY_TO_FLOAT,1),
OP(SM_CNV_ANY_TO_DOUBLE,1),
OP(SM_CNV_ANY_TO_STRING,1),
OP(SM_CNV_ANY_TO_BOOL,1),
OP(SM_CNV_ANY_TO_BINARY,1),
OP(SM_CNV_ANY_TO_DATE,1),
OP(SM_CNV_ANY_TO_TIME,1),
OP(SM_CNV_ANY_TO_DATETIME,1),
OP(SM_CNV_ANY_TO_CHAR,1),
OP(SM_CNV_ANY_TO_HANDLE,1),
OP(SM_CNV_ANY_TO_ENUM,2),
OP(SM_CNV_ANY_TO_OBJECT,2),
OP(SM_CONV_DEC_RVALUE_TO_PTR,1),
OP(SM_COPY_EXPR_DEC,1),
OP(SM_CREATE_EXT_OBJ,2),
OP(SM_GLOBFUNCCALL_ANY,3),
OP(SM_SYSFUNCCALL_ANY,2),
OP(SM_DLLFUNCCALL_ANY,3),
OP(SM_DOTFUNCCALL_ANY,4),
OP(SM_PUSH_LOCAL_VAR_ANY,1),
OP(SM_PUSH_SHARED_VAR_ANY,1),
OP(SM_PUSH_LOCAL_GLOBREF_ANY,1),
OP(SM_PUSH_LOCAL_ARGREF_ANY,1),
OP(SM_PUSH_SHARED_GLOBREF_ANY,1),
OP(SM_ADD_ANY,0),
OP(SM_SUB_ANY,0),
OP(SM_MULT_ANY,0),
OP(SM_DIV_ANY,0),
OP(SM_POWER_ANY,0),
OP(SM_NEGATE_ANY,0),
OP(SM_EQ_ANY,0),
OP(SM_NE_ANY,0),
OP(SM_GT_ANY,0),
OP(SM_LT_ANY,0),
OP(SM_GE_ANY,0),
OP(SM_LE_ANY,0),
OP(SM_AND_ANY,0),
OP(SM_OR_ANY,0),
OP(SM_NOT_ANY,0),
OP(SM_DOT_ANY,1),
OP(SM_INDEX_ANY,0),
OP(SM_INDEX_ERR_CHK_ANY,0),
OP(SM_INT,0),
OP(SM_ABS_LONG,0),
OP(SM_ABS_DOUBLE,0),
OP(SM_ASC,1),
OP(SM_BLOB,2),
OP(SM_CEILING,0),
OP(SM_COS,0),
OP(SM_EXP,0),
OP(SM_FACT,0),
OP(SM_INTHIGH,0),
OP(SM_INTLOW,0),
OP(SM_ISDATE,1),
OP(SM_ISNULL,1),
OP(SM_ISNUMBER,1),
OP(SM_ISTIME,1),
OP(SM_ISVALID,0),
OP(SM_LEN_STRING,1),
OP(SM_LEN_BINARY,1),
OP(SM_LOG,0),
OP(SM_LOGTEN,0),
OP(SM_LOWER,1),
OP(SM_PI,0),
OP(SM_RAND_LONG,0),
OP(SM_RAND_DOUBLE,0),
OP(SM_SIN,0),
OP(SM_SQRT,0),
OP(SM_TAN,0),
OP(SM_UPPER,1),
OP(SM_CONV_TO_REFPAK,0),
OP(SM_PUSH_LOCAL_GLOBREF_RP,1),
OP(SM_PUSH_LOCAL_ARGREF_RP,1),
OP(SM_PUSH_SHARED_GLOBREF_RP,1),
OP(SM_PUSH_LOCAL_VAR_RP,1),
OP(SM_PUSH_SHARED_VAR_RP,1),
OP(SM_TRANSFORM_BOUNDED_TO_BOUNDED,5),
OP(SM_TRANSFORM_BOUNDED_TO_UNBOUNDED,1),
OP(SM_TRANSFORM_UNBOUNDED_TO_BOUNDED,4),
OP(SM_TRANSFORM_UNBOUNDED_TO_UNBOUNDED,1),
OP(SM_CALC_UNBOUNDED_ARRAY_BOUND,0),
OP(SM_CALC_SIMPLE_ARRAY_BOUND,2),
OP(SM_CALC_COMPLEX_ARRAY_BOUND,3),
OP(SM_BUILD_UNBOUNDED_ARRAYLIST,3),
OP(SM_BUILD_BOUNDED_ARRAYLIST,5),
OP(SM_TRANSFORM_ARRAYLIST_TO_UNBOUNDED,3),
OP(SM_TRANSFORM_ARRAYLIST_TO_BOUNDED,5),
OP(SM_FREE_REF_PAK_N,1),
OP(SM_ARRAY_BOUND_INFO,4),
OP(SM_LOWERBOUND,2),
OP(SM_UPPERBOUND,2),
OP(SM_INCR_ANY,2),
OP(SM_DECR_ANY,2),
OP(SM_PUSH_FUNC_CLASS,2),
OP(SM_CLASS_CALL,3),
OP(SM_CLASS_CALL_DEC,3),
OP(SM_CLASS_CALL_DOUBLE,3),
OP(SM_CLASS_CALL_ANY,3),
OP(SM_INDEX_RP,0),
OP(SM_DBDELETEWITHCURS,3),
OP(SM_DBEXECUTEIMMED,0),
OP(SM_DBEXECDYNWITHDESC,2),
OP(SM_DBFETCHWITHDESC,2),
OP(SM_DBOPENDYNWITHDESC,2),
OP(SM_DBUPDATEWITHCURS,3),
OP(SM_CREATE_USING,1),
OP(SM_TRANSFORM_ANY_TO_UNBOUNDED,1),
OP(SM_TRANSFORM_ANY_TO_BOUNDED,4),
OP(SM_FREE_INV_METH_ARGS,3),
OP(SM_PUSH_NULL,1),
OP(SM_COPY_LVALUE_ANY,1),
OP(SM_ENTER_EMBEDDED,1),
OP(SM_EXIT_EMBEDDED,0),
OP(SM_DOT_FLD_UPDATE_INDEX_RP,0),
OP(SM_CNV_STRING_TO_BOUNDED_CHARARRAY,2),
OP(SM_PUSH_NTH_PARENT,1),
OP(SM_MOD_LONG,0),
OP(SM_MOD_ULONG,0),
OP(SM_MOD_DOUBLE,0),
OP(SM_MOD_DEC,0),
OP(SM_MOD_ANY,0),
OP(SM_ABS_DEC,0),
OP(SM_ABS_ANY,0),
OP(SM_CEILING_ANY,0),
OP(SM_MIN_LONG,0),
OP(SM_MIN_ULONG,0),
OP(SM_MIN_DOUBLE,0),
OP(SM_MIN_DEC,0),
OP(SM_MIN_ANY,0),
OP(SM_MAX_LONG,0),
OP(SM_MAX_ULONG,0),
OP(SM_MAX_DOUBLE,0),
OP(SM_MAX_DEC,0),
OP(SM_MAX_ANY,0),
OP(SM_PUSH_TRY,2),
OP(SM_POP_TRY,0),
OP(SM_CATCH_EXCEPTION,0),
OP(SM_THROW_EXCEPTION,0),
OP(SM_GOSUB,1),
OP(SM_RETURN_SUB,0),
OP(SM_CNV_INT_TO_LONGLONG,1),
OP(SM_CNV_UINT_TO_LONGLONG,1),
OP(SM_CNV_LONG_TO_LONGLONG,1),
OP(SM_CNV_ULONG_TO_LONGLONG,1),
OP(SM_CNV_DEC_TO_LONGLONG,1),
OP(SM_CNV_FLOAT_TO_LONGLONG,1),
OP(SM_CNV_DOUBLE_TO_LONGLONG,1),
OP(SM_CNV_LONGLONG_TO_INT,1),
OP(SM_CNV_LONGLONG_TO_UINT,1),
OP(SM_CNV_LONGLONG_TO_LONG,1),
OP(SM_CNV_LONGLONG_TO_ULONG,1),
OP(SM_CNV_LONGLONG_TO_DEC,1),
OP(SM_CNV_LONGLONG_TO_FLOAT,1),
OP(SM_CNV_LONGLONG_TO_DOUBLE,1),
OP(SM_ADD_LONGLONG,0),
OP(SM_SUB_LONGLONG,0),
OP(SM_MULT_LONGLONG,0),
OP(SM_DIV_LONGLONG,0),
OP(SM_POWER_LONGLONG,0),
OP(SM_NEGATE_LONGLONG,0),
OP(SM_PUSH_CONST_LONGLONG,2),
OP(SM_PUSH_LOCAL_VAR_LONGLONG,1),
OP(SM_PUSH_LOCAL_GLOBREF_LONGLONG,1),
OP(SM_PUSH_LOCAL_ARGREF_LONGLONG,1),
OP(SM_PUSH_SHARED_VAR_LONGLONG,1),
OP(SM_PUSH_SHARED_GLOBREF_LONGLONG,1),
OP(SM_ASSIGN_LONGLONG,1),
OP(SM_COPY_CONST_LONGLONG,1),
OP(SM_ADDASSIGN_LONGLONG,2),
OP(SM_SUBASSIGN_LONGLONG,2),
OP(SM_MULTASSIGN_LONGLONG,2),
OP(SM_INCR_LONGLONG,2),
OP(SM_DECR_LONGLONG,2),
OP(SM_COPY_LVALUE_LONGLONG,1),
OP(SM_ABS_LONGLONG,0),
OP(SM_RAND_LONGLONG,0),
OP(SM_EQ_LONGLONG,0),
OP(SM_NE_LONGLONG,0),
OP(SM_GT_LONGLONG,0),
OP(SM_LT_LONGLONG,0),
OP(SM_GE_LONGLONG,0),
OP(SM_LE_LONGLONG,0),
OP(SM_MOD_LONGLONG,0),
OP(SM_MIN_LONGLONG,0),
OP(SM_MAX_LONGLONG,0),
OP(SM_GLOBFUNCCALL_LONGLONG,3),
OP(SM_SYSFUNCCALL_LONGLONG,2),
OP(SM_DLLFUNCCALL_LONGLONG,3),
OP(SM_DOTFUNCCALL_LONGLONG,4),
OP(SM_CLASS_CALL_LONGLONG,3),
OP(SM_COPY_EXPR_LONGLONG,1),
OP(SM_DOT_LONGLONG,1),
OP(SM_INDEX_LONGLONG,0),
OP(SM_CNV_ANY_TO_LONGLONG,1),
OP(SM_CONV_LONGLONG_RVALUE_TO_PTR,1),
OP(SM_INDEX_ERR_CHK_LONGLONG,0),
OP(SM_PUSH_CONST_BYTE,1),
OP(SM_CNV_INT_TO_BYTE,1),
OP(SM_CNV_UINT_TO_BYTE,1),
OP(SM_CNV_LONG_TO_BYTE,1),
OP(SM_CNV_ULONG_TO_BYTE,1),
OP(SM_CNV_DEC_TO_BYTE,1),
OP(SM_CNV_FLOAT_TO_BYTE,1),
OP(SM_CNV_DOUBLE_TO_BYTE,1),
OP(SM_CNV_ANY_TO_BYTE,1),
OP(SM_CNV_LONGLONG_TO_BYTE,1),
OP(SM_CNV_BYTE_TO_INT,1),
OP(SM_CNV_BYTE_TO_UINT,1),
OP(SM_CNV_BYTE_TO_LONG,1),
OP(SM_CNV_BYTE_TO_ULONG,1),
OP(SM_CNV_BYTE_TO_DEC,1),
OP(SM_CNV_BYTE_TO_FLOAT,1),
OP(SM_CNV_BYTE_TO_DOUBLE,1),
OP(SM_CNV_BYTE_TO_LONGLONG,1),
OP(SM_ADD_BYTE,0),
OP(SM_SUB_BYTE,0),
OP(SM_MULT_BYTE,0),
OP(SM_DIV_BYTE,0),
OP(SM_POWER_BYTE,0),
OP(SM_NEGATE_BYTE,0),
OP(SM_INCR_BYTE,2),
OP(SM_DECR_BYTE,2),
OP(SM_ASSIGN_BYTE,1),
OP(SM_ADDASSIGN_BYTE,2),
OP(SM_SUBASSIGN_BYTE,2),
OP(SM_MULTASSIGN_BYTE,2),
OP(SM_EQ_BYTE,0),
OP(SM_NE_BYTE,0),
OP(SM_GT_BYTE,0),
OP(SM_LT_BYTE,0),
OP(SM_GE_BYTE,0),
OP(SM_LE_BYTE,0)
};
