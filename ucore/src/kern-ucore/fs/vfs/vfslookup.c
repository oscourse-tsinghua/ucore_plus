/*
 * VFS operations relating to pathname translation
 */

#include <types.h>
#include <string.h>
#include <inode.h>
#include <error.h>
#include <assert.h>
#include <stat.h>
#include <slab.h>
#include <iobuf.h>
#include <kio.h>

#include "vfs.h"
#include "vfsmount.h"

/*
 * Common code to pull the device name, if any, off the front of a
 * path and choose the inode to begin the name lookup relative to.
 */
static int get_device(char *path, char *subpath, struct inode **node_store)
{
  int ret;

  //Get the inode and fs of current directory.
  struct inode* current_dir_node;
  ret = vfs_get_curdir(&current_dir_node);
  if(ret != 0) return ret;
  struct fs* current_dir_fs = vop_fs(current_dir_node);

  //Get the path and fs we are changing into.
  static char full_path[4096];
  strcpy(full_path, path);
  vfs_fullpath(full_path, 4096);
  char* new_path;
  struct fs* new_path_fs;
  vfs_mount_parse_full_path(full_path, &new_path, &new_path_fs);

  //If we are actually changing fs
  if(new_path_fs != current_dir_fs) {
    //Then anyway we need to lookup from the root of the new fs.
    (*node_store) = fsop_get_root(new_path_fs);
    if(*new_path == '\0') {
      strcpy(subpath, ".");
    }
    else {
      strcpy(subpath, new_path);
    }
    return 0;
  }
  //Otherwise, check whether path is absolute or relative
  else {
    if(path[0] == '/') {
      //For absolute path, look up from root inode (use new_path is required).
      (*node_store) = fsop_get_root(new_path_fs);
      strcpy(subpath, new_path);
    }
    else {
      //Otherwise, lookup from current directory
      (*node_store) = current_dir_node;
      strcpy(subpath, path);
    }
    return 0;
  }
}

/*
 * Name-to-inode translation.
 * (In BSD, both of these are subsumed by namei().)
 */

int vfs_lookup(const char *path, struct inode **node_store, bool follow_symlink)
{
	int ret;
  int node_type;
  //TODO: Security issue: this may lead to buffer overflow.
  char *full_path = kmalloc(4096);
  char *full_path_expand_buffer;
  char *symbolic_link_buffer;
  strcpy(full_path, path);
  for(;;) {
    char *sub_path;
    vfs_fullpath(full_path, 4096);
    struct inode* last_node;
    struct inode* current_node;
    struct fs* new_path_fs;
    vfs_mount_parse_full_path(full_path, &sub_path, &new_path_fs);
    current_node = fsop_get_root(new_path_fs);
    vop_ref_inc(current_node);
    int current_path_seg_begin, current_path_seg_end;
    current_path_seg_begin = sub_path - full_path;
    bool symbolic_link_found = false;
    if(full_path[current_path_seg_begin] == '\0') {
      *node_store = current_node;
      return 0;
    }
    while(!symbolic_link_found) {
      current_path_seg_end = current_path_seg_begin;
      while(full_path[current_path_seg_end + 1] != '/' && full_path[current_path_seg_end + 1] != '\0') {
        current_path_seg_end++;
      }
      char next_seg = full_path[current_path_seg_end + 1];
      full_path[current_path_seg_end + 1] = '\0';
      last_node = current_node;
      ret = vop_lookup(current_node, full_path + current_path_seg_begin, &current_node);
      full_path[current_path_seg_end + 1] = next_seg;
      if(ret != 0) {
        kfree(full_path);
        vop_ref_dec(current_node);
        return ret;
      }
      vop_ref_dec(last_node);
      ret = vop_gettype(current_node, &node_type);
      if(ret != 0) {
        kfree(full_path);
        return ret;
      }
      char temp;
      switch(node_type) {
        case S_IFLNK:
          if(!follow_symlink && next_seg == '\0') {
            *node_store = current_node;
            return 0;
          }
          else {
            symbolic_link_found = true;
            full_path_expand_buffer = kmalloc(4096);
            full_path_expand_buffer[0] = '\0';
            symbolic_link_buffer = kmalloc(4096);
            vop_readlink(current_node, symbolic_link_buffer);
            if(symbolic_link_buffer[0] != '/') {
              temp = full_path[current_path_seg_begin];
              full_path[current_path_seg_begin] = '\0';
              strcat(full_path_expand_buffer, full_path);
              full_path[current_path_seg_begin] = temp;
            }
            strcat(full_path_expand_buffer, symbolic_link_buffer);
            strcat(full_path_expand_buffer, full_path + current_path_seg_end + 1);
            strcpy(full_path, full_path_expand_buffer);
            kfree(symbolic_link_buffer);
            kfree(full_path_expand_buffer);
            break;
          }
        case S_IFDIR:
          if(next_seg != '\0') {
            current_path_seg_begin = current_path_seg_end + 2;
          }
          else {
            *node_store = current_node;
            return 0;
          }
          break;
        default:
          if(next_seg != '\0') {
            vop_ref_dec(current_node);
            return -E_INVAL;
          }
          else {
            *node_store = current_node;
            return 0;
          }
      }
    }
  }
  panic("Arriving at an impossible place.");
  return -E_INVAL;
}

int vfs_lookup_parent(char *path, struct inode **node_store, char **endp)
{
  int ret;
	struct inode *node;
  //TODO: Security issue: this may lead to buffer overflow.
  static char subpath[1024];
	if ((ret = get_device(path, subpath, &node)) != 0) {
		return ret;
	}
	ret =
	    (*path != '\0' && *path != '/') ? vop_lookup_parent(node, subpath, node_store,
						endp) : -E_INVAL;
	vop_ref_dec(node);
	return ret;
}
