#include <kdebug.h>
#include <error.h>
#include <assert.h>
#include <devfs/devfs.h>
#include <inode.h>
#include <dev.h>
#include <iobuf.h>
#include <types.h>
#include <string.h>
#include <mmu.h>
#include <slab.h>
#include <mbr.h>
#include <kio.h>

const static int MBR_SIZE = 512;

void dev_disk_partition_probe()
{
  for(list_entry_t* i = list_next(&devfs_device_list);
  i != &devfs_device_list; i = list_next(i)) {
    struct devfs_device* device = container_of(i, struct devfs_device, list_entry);
    struct inode* dev_node = device->inode;
    struct device* dev = vop_info(dev_node, device);
    int block_size = dev->d_blocksize;
    if(block_size < MBR_SIZE) {
      continue;
    }
    if(dop_open(dev, 0) != 0) continue;
    char *buffer = kmalloc(block_size);
    struct iobuf iob;
    iobuf_init(&iob, buffer, block_size, 0);
    dop_io3(dev, &iob, 0);
    dop_close(dev);
    struct mbr *mbr = (struct mbr *)buffer;
    if(mbr->signature[0] != 0x55 || mbr->signature[1] != 0xAA) continue;
    for(int i = 0; i < 4; i++) {
      kprintf("%x\n", mbr->partitions[i].status);
      kprintf("%x\n", mbr->partitions[i].begin_sector);
      //kprintf("%x\n", mbr->partitions[i].end_sector);
    }
    kprintf("[dev_diskpart] Probing %s %x %d\n", device->name, device->inode->in_type, block_size);
    kprintf("%d\n", sizeof(struct mbr));
    kprintf("%x\n", mbr->signature);
    /*if(strcmp(record->mountpoint, mountpoint) == 0) {
      (*record_store) = record;
      return 0;
    }*/
  }
  //(*record_store) = NULL;
  return -E_INVAL;
}

void dev_init_disk_partition(void)
{
  dev_disk_partition_probe();
  //panic("dev_init_disk_partition");
}
