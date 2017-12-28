#include <stdlib.h>
#include <string.h>
#include <slab.h>

void free(void* ptr) {
  kfree(ptr);
}

void* malloc(int size) {
  return kmalloc(size);
}

void* calloc (size_t num, size_t size) {
  size_t bytes = size * num;
  void* ret = malloc(bytes);
  memset(ret, 0, bytes);
  return ret;
}

void yaffs_qsort(void *aa, size_t n, size_t es, int (*cmp)(const void *, const void *));
