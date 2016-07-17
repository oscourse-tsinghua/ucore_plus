/*
 * VFS operations relating to pathname translation
 */

#include <types.h>
#include <string.h>
#include <vfs.h>
#include <inode.h>
#include <error.h>
#include <assert.h>
#include <stat.h>
#include <slab.h>
#include <iobuf.h>
#include <kio.h>

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
  static char full_path[1024];
  if(path[0] != "/") {
    vfs_expand_path(path, full_path, 1024);
    vfs_simplify_path(full_path);
  }
  else {
    strcpy(full_path, path);
  }
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

int vfs_lookup(char *path, struct inode **node_store)
{
	int ret;
	struct inode *node;
  //TODO: Security issue: this may lead to buffer overflow.
  static char fullpath[1024];
  static char subpath[1024];
  if ((ret = get_device(path, subpath, &node)) != 0) {
    return ret;
  }
  for(;;) {
    if (*subpath != '\0') {
      ret = vop_lookup(node, subpath, node_store);
      int node_type;
      if(ret != 0) {
        vop_ref_dec(node);
        return ret;
      }
      ret = vop_gettype(*node_store, &node_type);
      vop_ref_dec(node);
      if(ret != 0) {
        return ret;
      }
      if(node_type != S_IFLNK) {
        return ret;
      }
      char* link_to_path = kmalloc(1024);
      struct iobuf iobuf;
      iobuf_init(&iobuf, link_to_path, 1024, 0);
      vop_read(*node_store, &iobuf);
      int link_to_path_length = iobuf_used(&iobuf);
      link_to_path[link_to_path_length] = '\0';
      strcpy(fullpath, subpath);
      strcat(fullpath, "/");
      strcat(fullpath, link_to_path);
      kfree(link_to_path);
      if ((ret = get_device(fullpath, subpath, &node)) != 0) {
        return ret;
      }
    }
    else {
      *node_store = node;
      return 0;
    }
  }
  //Impossible to arrive here.
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
	    (*path != '\0') ? vop_lookup_parent(node, subpath, node_store,
						endp) : -E_INVAL;
	vop_ref_dec(node);
	return ret;
}
