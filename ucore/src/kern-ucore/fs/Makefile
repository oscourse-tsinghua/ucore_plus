dirs-y := devs devfs pipe vfs swap lwext4/src
obj-y :=  kernel_file_pool.o file_desc_table.o file.o fs.o iobuf.o sysfile.o

dirs-$(UCONFIG_HAVE_SFS) += sfs
dirs-$(UCONFIG_HAVE_YAFFS2) += yaffs2_direct
dirs-$(UCONFIG_HAVE_FATFS) += fatfs
dirs-$(UCONFIG_HAVE_FUSE) += fuse
