#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29

#include <fuse.h>

#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"
#include "util.h"

/**
 * look up the file/dir from path path in file system fs, once found, stores the inode index of the file into inode_i
 *
 * @param path 	    the path to the file
 * @param fs        a pointer to the file system
 * @param inode_i   stores the inode index of the file
 * @return          0 on success;
 */
int path_lookup(const char *path, fs_ctx *fs, a1fs_ino_t *inode_i);

/**
 * get the name path of the parent dir and child file/dir name from the path path
 *
 * @param parent 	stores path of the parent dir
 * @param child     stores name of the child file/dir
 * @param path      path given
 * @return          0 on success; -1 otherwise
 */
int get_parent_child_str_from_path(char **parent, char **child, const char *path);

/**
 * create an inode with mode mode in file system fs
 *
 * @param mode 	    the mode of the file
 * @param fs        a pointer to the file system
 * @return          index of the inode
 */
a1fs_ino_t create_inode(fs_ctx *fs, mode_t mode);

/**
 * add the dentry dentry to dir at inode index dir_i in file system fs
 *
 * @param dir_i     inode index of the dir
 * @param dentry    the dentry struct to be added
 * @param fs        a pointer to the file system
 */
void add_dentry(a1fs_ino_t dir_i, a1fs_dentry dentry, fs_ctx *fs);

/**
 * create a dentry struct specified by inode index ino and name name
 *
 * @param ino               inode index of the file/dir
 * @param name              name of the file/dir
 * @return                  a dentry struct specified by ino and name
 */
a1fs_dentry create_dentry(a1fs_ino_t ino, char *name);

/**
 * a ceiling function for x/y
 *
 * @param x 	    numerator
 * @param y         denominator
 * @return          ceil(x/y)
 */
uint32_t divide_ceil(uint32_t x, uint32_t y);

/**
 * remove a dentry struct dentry in dir at inode index dir_i in file system fs
 *
 * @param dir_i             inode index of the dir
 * @param dentry            dentry struct to be removed
 * @param fs                a pointer to the file system
 */
void rm_dentry(a1fs_ino_t dir_i, a1fs_dentry dentry, fs_ctx *fs);

/**
 * find the pointer to the byte specified by offset of the file with inode index ino in file system fs
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @param offset            offset of the file
 * @return                  pointer to the byte
 */
unsigned char *find_offset(a1fs_ino_t ino, fs_ctx *fs, uint32_t offset);

/**
 * get the pointer to the last blk of the file with inode index ino in file system fs
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @return                  the pointer to the last blk
 */
unsigned char *find_last_blk(a1fs_ino_t ino, fs_ctx *fs);

/**
 * extend the file by filling extend_size 0s at the end of the file with inode index ino_i in file system fs
 *
 * @param extend_size 	    bytes to extend
 * @param ino_i             inode index of the file
 * @param fs                a pointer to the file system
 * @return                  0 on success; -errno on error.
 */
int extend_file(uint32_t extend_size, a1fs_ino_t ino_i, fs_ctx *fs);

/**
 * delete all the data of the file with inode index ino in file system fs
 * assuming bytes_to_delete > 0
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 */
void delete_file_data(a1fs_ino_t ino, fs_ctx *fs);

/**
 * truncate the file with inode index ino in the file system fs by bytes_to_delete bytes
 * assuming bytes_to_delete > 0
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @param bytes_to_delete   bytes to truncate
 */
void truncate_file(a1fs_ino_t ino, fs_ctx *fs, int bytes_to_delete);

/**
 * flip the bitmap to 0 at index index and of size size
 * free_inodes_count and free_blocks_count are also updated
 *
 * @param map           'i' represents inode bitmap to be flipped; 'd' represents data bitmap
 * @param index         start index of the bits to be flipped
 * @param size          number of bits to be flipped
 * @param fs            pointer to the file system
 */
void unset_bitmap(unsigned char map, uint32_t index, uint32_t size, fs_ctx *fs);