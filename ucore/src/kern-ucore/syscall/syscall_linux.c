#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <vmm.h>
#include <clock.h>
#include <error.h>
#include <assert.h>
#include <sem.h>
#include <event.h>
#include <mbox.h>
#include <stat.h>
#include <dirent.h>
#include <sysfile.h>
#include <kio.h>
#include <file.h>

machine_word_t syscall_linux_read(machine_word_t args[])
{
	int fd = (int)args[0];
	void *base = (void *)args[1];
	size_t len = (size_t)args[2];
	return sysfile_read(fd, base, len);
}

machine_word_t syscall_linux_write(machine_word_t args[])
{
	int fd = (int)args[0];
	void *base = (void *)args[1];
	size_t len = (size_t)args[2];
	return sysfile_write(fd, base, len);
}

machine_word_t syscall_linux_open(machine_word_t args[])
{
	const char *path = (const char *)args[0];
	uint32_t open_flags = (uint32_t)args[1];
	return sysfile_open(path, open_flags);
}

machine_word_t syscall_linux_close(machine_word_t args[])
{
	int fd = (int)args[0];
	return sysfile_close(fd);
}

machine_word_t syscall_linux_stat(machine_word_t args[])
{
	char *fn = (char *)args[0];
	struct linux_stat *st = (struct linux_stat *)args[1];
	return sysfile_linux_stat(fn, st);
}

machine_word_t syscall_linux_lstat(machine_word_t args[])
{
	char *fn = (char *)args[0];
	struct linux_stat *st = (struct linux_stat *)args[1];
  //TODO: lstat should be handling symbolic link in a different way than stat.
  //This is a temporary workaround.
	return sysfile_linux_stat(fn, st);
}

machine_word_t syscall_linux_fstat(machine_word_t args[])
{
	int fd = (char *)args[0];
	struct linux_stat *st = (struct linux_stat *)args[1];
	return sysfile_linux_fstat(fd, st);
}

machine_word_t syscall_linux_mount(machine_word_t args[])
{
	const char *source = (const char *)args[0];
	const char *target = (const char *)args[1];
	const char *filesystemtype = (const char *)args[2];
  unsigned long mountflags = (unsigned long)args[3];
	const void *data = (const void *)args[4];
	return do_mount(source, target, filesystemtype, mountflags, data);
}

machine_word_t syscall_linux_umount(machine_word_t args[])
{
	const char *target = (const char *)args[0];
  int flags = (int)args[1];
	return do_umount(target);
}

#ifndef __UCORE_64__

machine_word_t syscall_linux_stat64(machine_word_t args[])
{
	char *fn = (char *)args[0];
	struct linux_stat64 *st = (struct linux_stat *)args[1];
	return sysfile_linux_stat64(fn, st);
}

machine_word_t syscall_linux_lstat64(machine_word_t args[])
{
	char *fn = (char *)args[0];
	struct linux_stat64 *st = (struct linux_stat *)args[1];
  //TODO: lstat should be handling symbolic link in a different way than stat.
  //This is a temporary workaround.
	return sysfile_linux_stat64(fn, st);
}

machine_word_t syscall_linux_fstat64(machine_word_t args[])
{
	int fd = (char *)args[0];
	struct linux_stat64 *st = (struct linux_stat *)args[1];
	return sysfile_linux_fstat64(fd, st);
}

#endif /* __UCORE_64__ */