
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
  struct lib_entry_private *next;
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
  struct lib_entry_private *first_ent;
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
  DEBUGF(LIB, "open(%s, O_RDONLY) = %d", filename, fd);
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
  struct library_private *lib = pool_alloc_type(pool, struct library_private);
  memset(&lib->root, 0, sizeof(lib->root));
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

  dir->nod = pool_alloc_type(lib->pool, struct nod);
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
  DEBUGF(LIB, "Read dir @%x, %u entries (first %s, last %s)",
    dir->offset, dir->nod->no_entries, dir->first, dir->last);
}

static struct directory * dir_left(struct library_private *lib, struct directory *dir){
  read_dir(lib, dir);
  if (!dir->left && dir->nod->left_offset){
    dir->left = pool_alloc_type(lib->pool, struct directory);
    memset(dir->left, 0, sizeof(struct directory));
    dir->left->offset = dir->nod->left_offset;
    DEBUGF(LIB, "Init left @%x",dir->left->offset);
  }
  return dir->left;
}

static struct directory * dir_right(struct library_private *lib, struct directory *dir){
  read_dir(lib, dir);
  if (!dir->right && dir->nod->right_offset){
    dir->right = pool_alloc_type(lib->pool, struct directory);
    memset(dir->right, 0, sizeof(struct directory));
    dir->right->offset = dir->nod->right_offset;
    DEBUGF(LIB, "Init right @%x",dir->right->offset);
  }
  return dir->right;
}

static void read_ents(struct library_private *lib, struct directory *dir){
  read_dir(lib, dir);
  if (!dir->nod->no_entries || dir->first_ent)
    return;

  struct lib_entry_private **ptr = &dir->first_ent;
  uint8_t *data = dir->nod->ent_start;
  unsigned index;

  for(index=0; index < dir->nod->no_entries; index++){
    struct lib_entry_private *entry = (*ptr) = pool_alloc_type(lib->pool, struct lib_entry_private);
    memset(entry, 0, sizeof(struct lib_entry_private));
    ptr = &entry->next;

    entry->lib = lib;
    if (lib->pub.unicode){
      struct ent_u *ent = (struct ent_u *)data;
      assert(strncmp(ent->type, ENT, 4)==0);

      //DUMP(data, sizeof(struct ent_u) + ent->name_len);
      data += sizeof(struct ent_u);

      entry->pub.name=pool_dupn_u(lib->pool, (UChar *)data, ent->name_len -2);
      entry->pub.length=ent->length;
      entry->pub.timestamp=ent->timestamp;
      entry->start_offset=ent->first_block;
      entry->comment_len=ent->comment_len;

      data += ent->name_len;
    }else{
      struct ent_a *ent = (struct ent_a *)data;
      assert(strncmp(ent->type, ENT, 4)==0);
      data += sizeof(struct ent_a);

      //DUMP(data, sizeof(struct ent_a) + ent->name_len);
      entry->pub.name=(char *)data;
      entry->pub.length=ent->length;
      entry->pub.timestamp=ent->timestamp;
      entry->start_offset=ent->first_block;
      entry->comment_len=ent->comment_len;

      data += ent->name_len;
    }
    //DEBUGF(LIB, "Parsed ent %s @%lx", entry->pub.name, (data - dir->nod->raw) + dir->offset);
    assert(data - dir->nod->raw < (long)sizeof(*dir->nod));
  }
  DEBUGF(LIB, "Read ent's for dir @%x",dir->offset);
}

static void read_ent_comment(struct lib_entry_private *ent){
  if (!ent->comment_len || ent->pub.comment)
    return;
  lseek(ent->lib->fd, ent->start_offset, SEEK_SET);
  struct dat dat;
  read(ent->lib->fd, &dat, sizeof(dat));
  assert(strncmp(dat.type, DAT, 4)==0);
  if (ent->lib->pub.unicode){
    ent->pub.comment = pool_dupn_u(ent->lib->pool, (UChar*)dat.data, ent->comment_len);
  }else{
    ent->pub.comment = pool_dupn(ent->lib->pool, (char*)dat.data, ent->comment_len);
  }
}

static void dir_enum(struct library_private *lib, struct directory *dir, entry_callback callback, void *context){
  if (!dir)
    return;
  dir_enum(lib, dir_left(lib, dir), callback, context);
  read_ents(lib, dir);
  struct lib_entry_private *entry = dir->first_ent;
  while(entry){
    read_ent_comment(entry);
    callback((struct lib_entry *)entry, context);
    entry = entry->next;
  }
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
      read_ents(lib, dir);
      struct lib_entry_private *entry = dir->first_ent;
      while(entry){
	if (strcmp(entry->pub.name, entry_name)==0){
	  DEBUGF(LIB, "Found %s", entry_name);
	  read_ent_comment(entry);
	  return (struct lib_entry *)entry;
	}
	entry = entry->next;
      }
      DEBUGF(LIB, "%s NOT FOUND", entry_name);
      return NULL;
    }
  }
  DEBUGF(LIB, "%s NOT FOUND", entry_name);
  return NULL;
}

size_t lib_entry_read(struct lib_entry *entry, uint8_t *buffer, size_t len){
  assert(entry);
  assert(buffer);

  struct lib_entry_private *ent = (struct lib_entry_private *)entry;

  if (!ent->dat){

    if (!ent->pub.length)
      return 0;

    ent->dat = pool_alloc_type(ent->lib->pool, struct dat);
    assert(ent->dat);
    // read the first block

    assert(ent->start_offset);
    lseek(ent->lib->fd, ent->start_offset, SEEK_SET);
    read(ent->lib->fd, ent->dat, sizeof(struct dat));
    ent->remaining = ent->pub.length;
    assert(strncmp(ent->dat->type, DAT, 4)==0);
    assert(ent->remaining >= ent->dat->length);
    // skip the file comment
    ent->block_offset = ent->comment_len;
    ent->remaining -= ent->dat->length;
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

  DUMP(RAWREAD, buffer, bytes_read);
  return bytes_read;
}
