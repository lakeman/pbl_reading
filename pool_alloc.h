
#ifndef pool_alloc_header
#define pool_alloc_header

#include <unistd.h>
#include <stddef.h>
#include <unicode/ustring.h>

struct pool;

struct pool *pool_create();
void pool_release(struct pool *pool);
void *pool_alloc(struct pool *pool, size_t size, unsigned alignment);

const char *pool_dup(struct pool *pool, const char *str);
const char *pool_dup_u(struct pool *pool, const UChar *str);

#define pool_alloc_struct(P, T) (T *) pool_alloc((P), sizeof(T), offsetof( struct { char x; T dummy; }, dummy))

#endif
