#include <types.h>
#include <string.h>
#include <slab.h>
#include <vfs.h>
#include <proc.h>
#include <file.h>
#include <unistd.h>
#include <iobuf.h>
#include <inode.h>
#include <stat.h>
#include <dirent.h>
#include <error.h>
#include <assert.h>
#include <vmm.h>

#include "file_desc_table.h"
#include "kernel_file_pool.h"

#define testfd(fd)                          ((fd) >= 0 && (fd) < FS_STRUCT_NENTRY)

void filemap_acquire(struct file *file)
{
	fopen_count_inc(file);
}

void filemap_release(struct file *file)
{
	assert(fopen_count(file) > 0);
	fopen_count_dec(file);
}

int file_init(struct file *file)
{
  file->readable = 0;
  file->writable = 0;
  file->pos = 0;
  file->io_flags = 0;
  file->node = NULL;
  atomic_set(&file->open_count, 0);
  return 0;
}

inline int fd2file(int fd, struct file **file_store)
{
  struct file_desc_table *desc_table = fs_get_desc_table(current->fs_struct);
  struct file *file = file_desc_table_get_file(desc_table, fd);
  *file_store = file;
  if(file == NULL) {
    return -E_BADF;
  }
  else {
    return 0;
  }
  //TODO: delete the following codes.
	/*if (testfd(fd)) {
		struct file *file = get_filemap() + fd;
		if (file->status == FD_OPENED && file->fd == fd) {
			*file_store = file;
			return 0;
		}
	}
	return -E_BADF;*/
}

struct file* fd2file_onfs(int fd, struct fs_struct *fs_struct)
{
  struct file_desc_table *desc_table = fs_get_desc_table(fs_struct);
	if(testfd(fd)) {
		assert(fs_struct != NULL && fs_count(fs_struct) > 0);
		struct file *file = file_desc_table_get_file(desc_table, fd);
    return file;
    /*if((file->status == FD_OPENED || file->status == FD_CLOSED) && file->fd == fd) {
			return file;
		}*/
	} else {
		panic("testfd() failed %d", fd);
	}
	return NULL;
}

bool file_testfd(int fd, bool readable, bool writable)
{
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return 0;
	}
	if (readable && !file->readable) {
		return 0;
	}
	if (writable && !file->writable) {
		return 0;
	}
	return 1;
}

int file_open(char *path, uint32_t open_flags)
{
	bool readable = 0, writable = 0;
	switch (open_flags & O_ACCMODE) {
	case O_RDONLY:
		readable = 1;
		break;
	case O_WRONLY:
		writable = 1;
		break;
	case O_RDWR:
		readable = writable = 1;
		break;
	default:
		return -E_INVAL;
	}
  //TODO: Implement other open flags.

  //Try to allocate a new kernel struct file object.
	struct file *file = kernel_file_pool_allocate();
  file_init(file);
  if(file == NULL) {
    return -E_NO_MEM;
  }
  file->readable = readable;
  file->writable = writable;

  //Allocate a new file descriptor
  struct file_desc_table *desc_table = fs_get_desc_table(current->fs_struct);
  int fd = file_desc_table_get_unused(desc_table);
  if(fd < 0) {
    return -E_MFILE;
  }

  int ret;
  //Allocate a new inode for the kernel file
	struct inode *node;
  ret = vfs_open(path, open_flags, &node);
	if (ret != 0) {
		kernel_file_pool_free(file);
		return ret;
	}

  //Initialize file state
	file->pos = 0;
	if (open_flags & O_APPEND) {
		struct stat __stat, *stat = &__stat;
		if ((ret = vop_fstat(node, stat)) != 0) {
			vfs_close(node);
			kernel_file_pool_free(file);
			return ret;
		}
		file->pos = stat->st_size;
	}

  //Associate the file descriptor with kernel file object
  file_desc_table_associate(desc_table, fd, file);

	file->node = node;
  file->io_flags = 0;
	return fd;
}

int file_stat(const char *path, struct stat *stat)
{
	int ret;
	struct inode *node;
	if ((ret = vfs_lookup(path, &node, true)) != 0) {
		return ret;
	}
	ret = vop_fstat(node, stat);
	vop_ref_dec(node);
	return ret;
}

int file_lstat(const char *path, struct stat *stat)
{
	int ret;
	struct inode *node;
	if ((ret = vfs_lookup(path, &node, false)) != 0) {
		return ret;
	}
	ret = vop_fstat(node, stat);
	vop_ref_dec(node);
	return ret;
}

//TODO: Rewrite this function.
int file_close(int fd)
{
  struct file_desc_table *desc_table = fs_get_desc_table(current->fs_struct);
  if(file_desc_table_get_file(desc_table, fd) == NULL) {
    return -E_BADF;
  }
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
  file_desc_table_dissociate(desc_table, fd);
	return 0;
}

/*
 * file_read - read file
 * */
int file_read(int fd, void *base, size_t len, size_t * copied_store)
{
	int ret;
	struct file *file;
	*copied_store = 0;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
	if (!file->readable) {
		return -E_INVAL;
	}
	filemap_acquire(file);

	struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
	ret = vop_read(file->node, iob, file->io_flags);

	size_t copied = iobuf_used(iob);
	file->pos += copied;
	*copied_store = copied;
	filemap_release(file);
	return ret;
}

int file_write(int fd, void *base, size_t len, size_t * copied_store)
{
	int ret;
	struct file *file;
	*copied_store = 0;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
	if (!file->writable) {
		return -E_INVAL;
	}
	filemap_acquire(file);

	struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
	ret = vop_write(file->node, iob, file->io_flags);

	size_t copied = iobuf_used(iob);
	file->pos += copied;
	*copied_store = copied;
	filemap_release(file);
	return ret;
}

int file_seek(int fd, off_t pos, int whence)
{
	struct stat __stat, *stat = &__stat;
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
	filemap_acquire(file);

	switch (whence) {
	case LSEEK_SET:
		break;
	case LSEEK_CUR:
		pos += file->pos;
		break;
	case LSEEK_END:
		if ((ret = vop_fstat(file->node, stat)) == 0) {
			pos += stat->st_size;
		}
		break;
	default:
		ret = -E_INVAL;
	}

	if (ret == 0) {
		if ((ret = vop_tryseek(file->node, pos)) == 0) {
			file->pos = pos;
		}
	}
	filemap_release(file);
	return ret;
}

int file_fstat(int fd, struct stat *stat)
{
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
	filemap_acquire(file);
	ret = vop_fstat(file->node, stat);
	filemap_release(file);
	return ret;
}

int file_fsync(int fd)
{
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
	filemap_acquire(file);
	ret = vop_fsync(file->node);
	filemap_release(file);
	return ret;
}

int file_getdirentry(int fd, struct dirent *direntp)
{
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
	filemap_acquire(file);

	struct iobuf __iob, *iob =
	    iobuf_init(&__iob, direntp->d_name, sizeof(direntp->d_name),
		       direntp->d_off);
	if ((ret = vop_getdirentry(file->node, iob)) == 0) {
		direntp->d_off += iobuf_used(iob);
	}
	filemap_release(file);
	return ret;
}

int file_getdirentry64(int fd, struct dirent64 *direntp)
{
	int ret;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return ret;
	}
  direntp->d_off = file->pos;
	filemap_acquire(file);

	struct iobuf __iob, *iob =
	    iobuf_init(&__iob, direntp->d_name, sizeof(direntp->d_name),
		       direntp->d_off);
	if ((ret = vop_getdirentry(file->node, iob)) == 0) {
		direntp->d_off += iobuf_used(iob);
	}
  direntp->d_type = 8;
	filemap_release(file);
  file->pos = direntp->d_off;
	return ret;
}

int file_dup(int fd1, int fd2)
{
	int ret;
	struct file *file;

  //If fd1 is invalid, return -E_BADF.
	if ((ret = fd2file(fd1, &file)) != 0) {
		return ret;
	}

  //fd1 and fd2 cannot be the same
  if (fd1 == fd2) {
    return -E_INVAL;
  }

  struct file_desc_table *desc_table = fs_get_desc_table(current->fs_struct);

  //If fd2 is an opened file, close it first. This is what dup2 on linux does.
  struct file *file2 = file_desc_table_get_file(desc_table, fd2);
  if(file2 != NULL) {
		kprintf("file_desc_table_get_unused called by dup2!\n");
    file_desc_table_dissociate(desc_table, fd2);
  }

  //If fd2 is NO_FD, a new fd will be assigned.
  if (fd2 == NO_FD) {
    fd2 = file_desc_table_get_unused(desc_table);
  }

  if (fd2 >= 0 && fd2 < desc_table->capacity) {
	//Now let fd2 become a duplication for fd1.
	file_desc_table_associate(desc_table, fd2, file);
  }

  //fd2 is returned.
	return fd2;
}

int file_pipe(int fd[])
{
  int ret;
  struct file_desc_table *desc_table = fs_get_desc_table(current->fs_struct);
	struct file *file[2] = { NULL, NULL };
  file[0] = kernel_file_pool_allocate();
  file[1] = kernel_file_pool_allocate();
  if(file[0] == NULL || file[1] == NULL) {
    ret = -E_NFILE;
    goto failed_cleanup;
  }
  file_init(file[0]);
  file_init(file[1]);
  if ((ret = pipe_open(&(file[0]->node), &(file[1]->node))) != 0) {
    ret = -E_INVAL;
    goto failed_cleanup;
  }
  file[0]->pos = 0;
  file[0]->readable = 1;
  file[0]->writable = 0;

  file[1]->pos = 0;
  file[1]->readable = 0;
  file[1]->writable = 1;

  fd[0] = file_desc_table_get_unused(desc_table);
  if(fd[0] == -1) {
    ret = -E_MFILE;
    goto failed_cleanup;
  }
  file_desc_table_associate(desc_table, fd[0], file[0]);
  fd[1] = file_desc_table_get_unused(desc_table);
  if(fd[1] == -1) {
    file_desc_table_dissociate(desc_table, fd[0]);
    ret = -E_MFILE;
    goto failed_cleanup;
  }
  file_desc_table_associate(desc_table, fd[1], file[1]);
	return 0;

failed_cleanup:
	if (file[0] != NULL) {
		kernel_file_pool_free(file[0]);
	}
	if (file[1] != NULL) {
		kernel_file_pool_free(file[1]);
	}
	return ret;
}

int file_mkfifo(const char *__name, uint32_t open_flags)
{
  panic("TODO: mkfifo not implemented.");
	/*bool readonly = 0;
	switch (open_flags & O_ACCMODE) {
	case O_RDONLY:
		readonly = 1;
	case O_WRONLY:
		break;
	default:
		return -E_INVAL;
	}

	int ret;
	struct file *file;
	if ((ret = filemap_alloc(NO_FD, &file)) != 0) {
		return ret;
	}

	char *name;
	const char *device = readonly ? "pipe:r_" : "pipe:w_";

	if ((name = stradd(device, __name)) == NULL) {
		ret = -E_NO_MEM;
		goto failed_cleanup_file;
	}

	if ((ret = vfs_open(name, open_flags, &(file->node))) != 0) {
		goto failed_cleanup_name;
	}
	file->pos = 0;
	file->readable = readonly, file->writable = !readonly;
	filemap_open(file);
	kfree(name);
	return file->fd;

failed_cleanup_name:
	kfree(name);
failed_cleanup_file:
	filemap_free(file);
	return ret;*/
}

/* linux devfile adaptor */
bool __is_linux_devfile(int fd)
{
	int ret = -E_INVAL;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return 0;
	}
	if (file->node && check_inode_type(file->node, device)
	    && dev_is_linux_dev(vop_info(file->node, device)))
		return 1;
	return 0;
}

/* *
 * linux_devfile_read - this function is used to read from device file
 *						such as console
 *
 * */
int linux_devfile_read(int fd, void *base, size_t len, size_t * copied_store)
{
	int ret = -E_INVAL;
	struct file *file;
	/* use 8byte int, in case of 64bit off_t
	 * config in linux kernel */
	int64_t offset;
	if ((ret = fd2file(fd, &file)) != 0) {
		return 0;
	}

	if (!file->readable) {
		return -E_INVAL;
	}
	filemap_acquire(file);
	offset = file->pos;
	struct device *dev = vop_info(file->node, device);
	assert(dev);
	ret = dev->d_linux_read(dev, base, len, (size_t *) & offset);
	if (ret >= 0) {
		*copied_store = (size_t) ret;
		file->pos += ret;
		ret = 0;
	}
	filemap_release(file);
	return ret;
}

int linux_devfile_write(int fd, void *base, size_t len, size_t * copied_store)
{
	int ret = -E_INVAL;
	struct file *file;
	/* use 8byte int, in case of 64bit off_t
	 * config in linux kernel */
	int64_t offset;
	if ((ret = fd2file(fd, &file)) != 0) {
		return 0;
	}

	if (!file->writable) {
		return -E_INVAL;
	}
	filemap_acquire(file);
	offset = file->pos;
	struct device *dev = vop_info(file->node, device);
	assert(dev);
	ret = dev->d_linux_write(dev, base, len, (size_t *) & offset);
	if (ret >= 0) {
		*copied_store = (size_t) ret;
		file->pos += ret;
		ret = 0;
	}
	filemap_release(file);
	return ret;
}

int linux_devfile_ioctl(int fd, unsigned int cmd, unsigned long arg)
{
	int ret = -E_INVAL;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return 0;
	}
	filemap_acquire(file);
	struct device *dev = vop_info(file->node, device);
	assert(dev);
	ret = dev->d_linux_ioctl(dev, cmd, arg);
	filemap_release(file);
	return ret;
}

void *linux_devfile_mmap2(void *addr, size_t len, int prot, int flags, int fd,
			  size_t pgoff)
{
	int ret = -E_INVAL;
	struct file *file;
	if ((ret = fd2file(fd, &file)) != 0) {
		return NULL;
	}
	filemap_acquire(file);
	struct device *dev = vop_info(file->node, device);
	assert(dev);
	void *r = dev->d_linux_mmap(dev, addr, len, prot, flags, pgoff);
	filemap_release(file);
	return r;
}

int linux_access(char *path, int amode)
{
	/* do nothing but return 0 */
	return 0;
}

void *linux_regfile_mmap2(void *addr, size_t len, int prot, int flags, int fd,
			  size_t off)
{
	int subret = -E_INVAL;
	struct mm_struct *mm = current->mm;
	assert(mm != NULL);
	if (len == 0) {
		return -1;
	}
	lock_mm(mm);

	uintptr_t start = ROUNDDOWN(addr, PGSIZE);
	len = ROUNDUP(len, PGSIZE);

	uint32_t vm_flags = VM_READ;
	if (prot & PROT_WRITE) {
		vm_flags |= VM_WRITE;
	}
	if (prot & PROT_EXEC) {
		vm_flags |= VM_EXEC;
	}
	if (flags & MAP_STACK) {
		vm_flags |= VM_STACK;
	}
	if (flags & MAP_ANONYMOUS) {
		vm_flags |= VM_ANONYMOUS;
	}

	subret = -E_NO_MEM;
	if (start == 0 && (start = get_unmapped_area(mm, len)) == 0) {
		goto out_unlock;
	}
	uintptr_t end = start + len;
	struct vma_struct *vma = find_vma(mm, start);
	if (vma == NULL || vma->vm_start >= end) {
		vma = NULL;
	} else if (!(flags & MAP_FIXED)) {
		start = get_unmapped_area(mm, len);
		vma = NULL;
	} else if (!(vma->vm_flags & VM_ANONYMOUS)) {
		goto out_unlock;
	} else if (vma->vm_start == start && end == vma->vm_end) {
		vma->vm_flags = vm_flags;
	} else {
    if(vma->vm_start > start || end > vma->vm_end) {
      goto out_unlock;
    }
		if ((subret = mm_unmap_keep_pages(mm, start, len)) != 0) {
			goto out_unlock;
		}
		vma = NULL;
	}
	if (vma == NULL
	    && (subret = mm_map(mm, start, len, vm_flags, &vma)) != 0) {
		goto out_unlock;
	}
	if (!(flags & MAP_ANONYMOUS)) {
    struct file_desc_table *desc_table = fs_get_desc_table(current->fs_struct);
    struct file* file = file_desc_table_get_file(desc_table, fd);
#ifdef ARCH_ARM
		vma_mapfile(vma, file, off << 12, current->fs_struct);
#else
    vma_mapfile(vma, file, off, current->fs_struct);
#endif
	}
	subret = 0;
out_unlock:
	unlock_mm(mm);
	return subret == 0 ? (void*)start : (void*)-1;
}

int filestruct_setpos(struct file *file, off_t pos)
{
	int ret = vop_tryseek(file->node, pos);
	if (ret == 0) {
		file->pos = pos;
	}
	return ret;
}

int filestruct_read(struct file *file, void *base, size_t len)
{
	struct iobuf __iob, *iob = iobuf_init(&__iob, base, len, file->pos);
	vop_read(file->node, iob);
	size_t copied = iobuf_used(iob);
	file->pos += copied;
	return copied;
}
