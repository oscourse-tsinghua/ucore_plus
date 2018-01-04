#ifndef __LIBS_MBR_H__
#define __LIBS_MBR_H__

#include <types.h>

struct mbr_chs_address {
  unsigned head : 8;
  unsigned sector : 6;
  unsigned cylinder : 10;
} __attribute__((packed));

struct mbr_partition_entry {
  uint8_t status;
  struct mbr_chs_address begin_chs_address; // Almost deprecated.
  uint8_t type;
  struct mbr_chs_address end_chs_address; // Almost deprecated.
  uint32_t begin_sector;
  uint32_t end_sector;
} __attribute__((packed));

struct mbr {
  uint8_t boot_code[446];
  struct mbr_partition_entry partitions[4];
  uint8_t signature[2];
} __attribute__((packed));

#endif
