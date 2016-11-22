
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <unicode/ustring.h>
#include "lib.h"
#include "pbl_types.h"
#include "pool_alloc.h"
#include "debug.h"

struct library_private;

// self contained, no dependance on struct ent_(a|u)
struct lib_entry_private{
  struct lib_entry pub;
  struct library_private *lib;
  uint32_t start_offset;
  uint16_t comment_len;
  struct dat *dat;
  uint16_t block_offset;
  uint32_t remaining;
};

struct directory{
  struct directory *left;
  struct directory *right;
  uint32_t offset;
  struct nod *nod;
  const char *first;
  const char *last;
};

struct library_private{
  struct library pub;
  struct pool *pool;
  int fd;
  uint32_t scc_info;
  uint32_t scc_length;
  struct directory root;
};

struct library *lib_open(const char *filename){
  int fd = open(filename, O_RDONLY);
  DEBUGF("open(%s, O_RDONLY) = %d", filename, fd);
  if (fd<0)
    return NULL;

  // TODO look for a TRL block first
  off_t header_offset = 0;

  lseek(fd, header_offset, SEEK_SET);
  union{
    struct file_header_a ansi;
    struct file_header_u unicode;
  }header;

  // read the HDR block
  read(fd, &header, sizeof(header));

  assert(strncmp(header.ansi.type, HDR, 4)==0);

  uint8_t unicode;
  if (strncmp(header.ansi.pb, "PowerBuilder", 14)==0){
    unicode=0;
  }else{
    UErrorCode status = U_ZERO_ERROR;
    char PB[14];
    u_strToUTF8(PB, sizeof PB, NULL, header.unicode.pb, -1, &status);
    assert(!U_FAILURE(status));
    assert(strncmp(PB, "PowerBuilder", 14)==0);
    unicode=1;
  }

  // use a single memory pool for the lifetime of the library struct.
  // se we can duplicate string buffers, and quickly throw them all away when we're done
  struct pool *pool = pool_create();
  struct library_private *lib = pool_alloc_struct(pool, struct library_private);
  memset(&lib->root, sizeof(lib->root), 0);
  lib->pool = pool;
  lib->fd = fd;
  lib->pub.unicode = unicode;
  if (unicode){
    lib->pub.comment = pool_dup_u(pool, header.unicode.comment);
    lib->pub.version = pool_dup_u(pool, header.unicode.version);
    lib->pub.timestamp = header.unicode.timestamp;
    lib->pub.filetype = header.unicode.filetype;
    lib->scc_info = header.unicode.scc_info;
    lib->scc_length = header.unicode.scc_length;
    lib->root.offset = header_offset+0x600;
  }else{
    lib->pub.comment = pool_dup(pool, header.ansi.comment);
    lib->pub.version = pool_dup(pool, header.ansi.version);
    lib->pub.timestamp = header.ansi.timestamp;
    lib->pub.filetype = header.ansi.filetype;
    lib->scc_info = header.ansi.scc_info;
    lib->scc_length = header.ansi.scc_length;
    lib->root.offset = header_offset+0x400;
  }
  lib->pub.filename = pool_dup(pool, filename);
  return (struct library *)lib;
}

void lib_close(struct library *library){
  assert(library);
  struct library_private *lib = (struct library_private *)library;
  close(lib->fd);
  pool_release(lib->pool);
}

static void read_dir(struct library_private *lib, struct directory *dir){
  if (dir->nod)
    return;

  dir->nod = pool_alloc_struct(lib->pool, struct nod);
  lseek(lib->fd, dir->offset, SEEK_SET);
  read(lib->fd, dir->nod, sizeof(struct nod));
  assert(strncmp(dir->nod->type, NOD, 4)==0);

  if (dir->nod->no_entries){
    if (lib->pub.unicode){
      dir->first = pool_dup_u(lib->pool, (const UChar *)&dir->nod->raw[dir->nod->first_name]);
      dir->last = pool_dup_u(lib->pool, (const UChar *)&dir->nod->raw[dir->nod->last_name]);
    }else{
      dir->first = (const char *)&dir->nod->raw[dir->nod->first_name];
      dir->last = (const char *)&dir->nod->raw[dir->nod->last_name];
    }
  }
}

static struct directory * dir_left(struct library_private *lib, struct directory *dir){
  read_dir(lib, dir);
  if (!dir->left && dir->nod->left_offset){
    dir->left = pool_alloc_struct(lib->pool, struct directory);
    memset(dir->left, sizeof(struct directory), 0);
    dir->left->offset = dir->nod->left_offset;
  }
  return dir->left;
}

static struct directory * dir_right(struct library_private *lib, struct directory *dir){
  read_dir(lib, dir);
  if (!dir->right && dir->nod->right_offset){
    dir->right = pool_alloc_struct(lib->pool, struct directory);
    memset(dir->right, sizeof(struct directory), 0);
    dir->right->offset = dir->nod->right_offset;
  }
  return dir->right;
}

static void dir_enum(struct library_private *lib, struct directory *dir, entry_callback callback, void *context){
  if (!dir)
    return;
  dir_enum(lib, dir_left(lib, dir), callback, context);
  DEBUGF("TODO enum from %s to %s", dir->first, dir->last);
  dir_enum(lib, dir_right(lib, dir), callback, context);
}

void lib_enumerate(struct library *library, entry_callback callback, void *context){
  struct library_private *lib = (struct library_private *)library;
  dir_enum(lib, &lib->root, callback, context);
}

struct lib_entry *lib_find(struct library *library, const char *entry_name){
  struct library_private *lib = (struct library_private *)library;
  struct directory *dir = &lib->root;
  while(dir){
    read_dir(lib, dir);
    if (!dir->nod->no_entries)
      break;
    if (strcmp(entry_name, dir->first)<0){
      dir = dir_left(lib, dir);
    }else if(strcmp(entry_name, dir->last)>0){
      dir = dir_right(lib, dir);
    }else{
      DEBUGF("TODO locate ENT* of %s between %s && %s", entry_name, dir->first, dir->last);
      return NULL;
    }
  }
  DEBUGF("%s NOT FOUND", entry_name);
  return NULL;
}

size_t lib_entry_read(struct lib_entry *entry, uint8_t *buffer, size_t len){
  assert(entry);
  assert(buffer);

  struct lib_entry_private *ent = (struct lib_entry_private *)entry;

  if (!ent->dat){

    if (!ent->remaining)
      return 0;

    ent->dat = pool_alloc_struct(ent->lib->pool, struct dat);
    assert(ent->dat);
    // setup so that we read the next block
    ent->dat->next_offset = ent->start_offset;
    ent->dat->length = 0;
  }

  size_t bytes_read = 0;

  while(bytes_read < len){
    if (ent->block_offset < ent->dat->length){
      // copy data bytes from previously read data
      uint16_t remain = ent->dat->length - ent->block_offset;
      if (remain > (len - bytes_read))
	remain = len - bytes_read;

      memcpy(&buffer[bytes_read], &ent->dat->data[ent->block_offset], remain);

      ent->block_offset += remain;
      bytes_read += remain;
    } else if(!ent->remaining) {
      break;
    } else {
      // read more data
      assert(ent->dat->next_offset);
      lseek(ent->lib->fd, ent->dat->next_offset, SEEK_SET);
      read(ent->lib->fd, ent->dat, sizeof(struct dat));
      assert(strncmp(ent->dat->type, DAT, 4)==0);
      assert(ent->remaining >= ent->dat->length);
      ent->block_offset = 0;
      ent->remaining -= ent->dat->length;
    }
  }

  return bytes_read;
}
