#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
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

  int mask = ((1<<alignment)-1);
  struct buffer *buff = pool->current;

  while(1){
    void *ret = buff->current;
    int rounding = ((uintptr_t)ret & mask);
    if (rounding){
      rounding = (mask + 1 - rounding);
      ret += rounding;
    }

    if (size + rounding < buff->remaining){
      buff->current+=size + rounding;
      buff->remaining-=size + rounding;
      //DEBUGF(ALLOC,"pool_alloc(%p, %zu, %u) = %p, [from %p, remaining %zu]", pool, size, alignment, ret, pool->current, pool->current->remaining);
      return ret;
    }

    if (!buff->next){
      size_t alloc = BLOCK_SIZE;
      // make sure we can service this allocation!
      if (alloc < size + sizeof(struct buffer)){
	// rounded up to the next block size.
	alloc = (size + sizeof(struct buffer) & ~(BLOCK_SIZE-1)) + BLOCK_SIZE;
      }
      struct buffer *b = malloc(alloc);
      DEBUGF(ALLOC,"malloc() = %p", b);
      assert(b);
      init(pool, b);
      b->remaining = alloc - sizeof(struct buffer);
      buff->next = b;
    }

    // stop checking a buffer once it is almost used.
    if (buff == pool->current && buff->remaining < 64)
      pool->current = buff->next;

    buff = buff->next;
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
  ret[dst_len]=0;
  assert(!U_FAILURE(status));
  return ret;
}

const char *pool_dup_u(struct pool *pool, const UChar *str){
  if (!str)
    return NULL;
  if (!*str)
    return "";

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
  ret[len]=0;
  assert(!U_FAILURE(status));
  return ret;
}

const char *pool_sprintf(struct pool *pool, const char *fmt, ...)
{
  va_list ap;
  va_list ap1;

  va_start(ap, fmt);

  va_copy(ap1, ap);
  int len = vsnprintf(NULL, 0, fmt, ap1);
  va_end(ap1);

  char *ret = pool_alloc(pool, len+1, 1);
  vsnprintf(ret, len+1, fmt, ap);

  va_end(ap);
  return ret;
}
