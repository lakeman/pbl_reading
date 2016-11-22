
#ifndef pbl_types_header
#define pbl_types_header

#include <stdint.h>

#define BLOCK_SIZE 512

const char *HDR="HDR*";
const char *TRL="TRL*";
const char *NOD="NOD*";
const char *ENT="ENT*";
const char *DAT="DAT*";
const char *FRE="FRE*";

#pragma pack(push,1)

//struct definitions to define file format layout

struct file_header_a{
  char type[4];
  char pb[14];
  char version[14];
  uint32_t timestamp;
  uint16_t filetype;
  char comment[256];
  uint32_t scc_info;
  uint32_t scc_length;
};

struct file_header_u{
  char type[4];
  UChar pb[14];
  UChar version[14];
  uint32_t timestamp;
  uint16_t filetype;
  UChar comment[256];
  uint32_t scc_info;
  uint32_t scc_length;
};

#define DAT_SIZE (BLOCK_SIZE - 10)

struct dat{
  char type[4];
  uint32_t next_offset;
  uint16_t length;
  uint8_t data[DAT_SIZE];
};

#define FRE_SIZE (BLOCK_SIZE - 8)

struct fre{
  char type[4];
  uint32_t next_offset;
  uint8_t data[FRE_SIZE];
};

#define NOD_SIZE (BLOCK_SIZE*6 - 32)

struct nod{
  union{
    struct{
      char type[4];
      uint32_t left_offset;
      uint32_t parent_offset;
      uint32_t right_offset;
      uint16_t remaining;
      uint16_t last_name;
      uint16_t no_entries;
      uint16_t first_name;
    };
    struct{
      uint8_t padding[32];
      uint8_t ent_start[0];
    };
    uint8_t raw[BLOCK_SIZE*6];
  };
};

struct ent_a{
  char type[4];
  char version[4];
  uint32_t first_block;
  uint32_t length;
  uint32_t timestamp;
  uint16_t comment_len;
  uint16_t name_len;
};

struct ent_u{
  char type[4];
  uint16_t version[4];
  uint32_t first_block;
  uint32_t length;
  uint32_t timestamp;
  uint16_t comment_len;
  uint16_t name_len;
};

#define MIN_ENT_SIZE ((sizeof(struct ent_a) + 5)

#pragma pack(pop)

#endif
