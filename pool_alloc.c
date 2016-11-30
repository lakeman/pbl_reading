#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "pool_alloc.h"
#include "debug.h"

#define BLOCK_SIZE (0x10000)

struct buffer{
  struct buffer *next;
  void *current;
  size_t remaining;
  uint8_t data[0];
};

struct pool{
  struct buffer *current;
  struct buffer first;
};

static void init(struct pool *pool, struct buffer *b){
  pool->current = b;
  b->next = NULL;
  b->current = &b->data[0];
}

struct pool *pool_create(){
  struct pool *pool = malloc(BLOCK_SIZE);
  assert(pool);
  DEBUGF(ALLOC,"malloc() = %p", pool);
  init(pool, &pool->first);
  pool->first.remaining = BLOCK_SIZE - sizeof(struct pool);
  //DEBUGF(ALLOC,"Created pool @%p, remaining %zu", pool, pool->current->remaining);
  return pool;
}

void pool_release(struct pool *pool){
  while(pool->first.next){
    struct buffer *t = pool->first.next;
    pool->first.next = pool->first.next->next;
    DEBUGF(ALLOC,"Free %p", t);
    free(t);
  }
  DEBUGF(ALLOC,"Free %p", pool);
  free(pool);
}

void *pool_alloc(struct pool *pool, size_t size, unsigned alignment){
  DEBUGF(ALLOC, "Allocating %zu", size);
  assert(size <= BLOCK_SIZE - sizeof(struct buffer));
  while(1){
    void *ret = pool->current->current;
    int mask = ((1<<alignment)-1);
    int rounding = ((uintptr_t)ret & mask);
    if (rounding){
      ret+=(mask + 1 - rounding);
      size+=(mask + 1 - rounding);
    }

    if (size > pool->current->remaining){
      struct buffer *b = malloc(BLOCK_SIZE);
      DEBUGF(ALLOC,"malloc() = %p", b);
      assert(b);
      pool->current->next = b;
      init(pool, b);
      b->remaining = BLOCK_SIZE - sizeof(struct buffer);
      continue;
    }

    pool->current->current+=size;
    pool->current->remaining-=size;
    //DEBUGF(ALLOC,"pool_alloc(%p, %zu, %u) = %p, [from %p, remaining %zu]", pool, size, alignment, ret, pool->current, pool->current->remaining);
    return ret;
  }
}

const char *pool_dupn(struct pool *pool, const char *str, size_t len){
  if (!str)
    return NULL;
  if (len==0)
    return NULL;
  char *ret = pool_alloc(pool, len+1, 1);
  strcpy(ret, str);
  return ret;
}

const char *pool_dup(struct pool *pool, const char *str){
  if (!str)
    return NULL;

  size_t len = strlen(str);
  return pool_dupn(pool, str, len);
}

const char *pool_dupn_u(struct pool *pool, const UChar *str, size_t len){
  if (!str)
    return NULL;

  UErrorCode status = U_ZERO_ERROR;

  int32_t dst_len=0;
  u_strToUTF8(NULL, 0, &dst_len, str, len/2, &status);
  if (dst_len==0){
    DEBUGF(ALLOC,"Failed to measure unicode string?");
    return NULL;
  }
  status = U_ZERO_ERROR;
  char *ret = pool_alloc(pool, dst_len+1, 1);
  u_strToUTF8(ret, dst_len, NULL, str, len/2, &status);
  assert(!U_FAILURE(status));
  return ret;
}

const char *pool_dup_u(struct pool *pool, const UChar *str){
  if (!str)
    return NULL;

  UErrorCode status = U_ZERO_ERROR;

  int32_t len=0;
  u_strToUTF8(NULL, 0, &len, str, -1, &status);
  if (len==0){
    DEBUGF(ALLOC,"Failed to measure unicode string?");
    return NULL;
  }
  status = U_ZERO_ERROR;
  char *ret = pool_alloc(pool, len+1, 1);
  u_strToUTF8(ret, len, NULL, str, -1, &status);
  assert(!U_FAILURE(status));
  return ret;
}
