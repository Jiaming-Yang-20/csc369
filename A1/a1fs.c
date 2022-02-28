/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2019 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

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
#include "helpers.h"

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".

/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
	// Nothing to initialize if only printing help
	if (opts->help)
		return true;

	size_t size;
	void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
	if (!image)
		return false;

	return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
	fs_ctx *fs = (fs_ctx *)ctx;
	if (fs->image)
	{
		munmap(fs->image, fs->size);
		fs_ctx_destroy(fs);
	}
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
	return (fs_ctx *)fuse_get_context()->private_data;
}

/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
	(void)path; // unused
	fs_ctx *fs = get_fs();
	memset(st, 0, sizeof(*st));
	st->f_bsize = A1FS_BLOCK_SIZE;
	st->f_frsize = A1FS_BLOCK_SIZE;
	//TODO: fill in the rest of required fields based on the information stored
	// in the superblock
	a1fs_superblock *sb = (a1fs_superblock *)(fs->image + A1FS_BLOCK_SIZE);
	st->f_blocks = sb->size / A1FS_BLOCK_SIZE;
	st->f_bfree = sb->s_free_blocks_count;
	st->f_bavail = sb->s_free_blocks_count;
	st->f_files = sb->s_inodes_count;
	st->f_ffree = sb->s_free_inodes_count;
	st->f_favail = sb->s_free_inodes_count;
	st->f_namemax = A1FS_NAME_MAX;

	return 0;
}

/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors);
 *       it should include any metadata blocks that are allocated to the 
 *       inode.
 *
 * NOTE2: the st_mode field must be set correctly for files and directories.
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
	if (strlen(path) >= A1FS_PATH_MAX)
		return -ENAMETOOLONG;
	fs_ctx *fs = get_fs();

	memset(st, 0, sizeof(*st));

	//TODO: lookup the inode for given path and, if it exists, fill in the
	// required fields based on the information stored in the inode

	(void)fs;
	a1fs_ino_t inode_i;
	// inode index for given path
	int error;
	if ((error = path_lookup(path, fs, &inode_i)) != 0)
	{
		return error;
	}
	a1fs_inode *inode = &(fs->root_ino[inode_i]);
	st->st_mode = inode->mode;
	st->st_nlink = inode->links;
	st->st_size = inode->size;
	st->st_blocks = divide_ceil(A1FS_BLOCK_SIZE, 512);
	st->st_mtim = inode->mtime;
	return 0;
}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
						off_t offset, struct fuse_file_info *fi)
{
	fs_ctx *fs = get_fs();
	(void) offset;
	(void) fi;
	//TODO: lookup the directory inode for given path and iterate through its
	// directory entries
	if (filler(buf, "..", NULL, 0) != 0){return -ENOMEM;}
	if (filler(buf, ".", NULL, 0) != 0){return -ENOMEM;}
	a1fs_ino_t inode_i;
	path_lookup(path, fs, &inode_i); // get the inode for the path
	a1fs_inode *inode = &(fs->root_ino[inode_i]);
	if (inode->size == 0)
	{
		return 0;
	}

	a1fs_extent *s_extent = (a1fs_extent *)(fs->data_blk + inode->s_extent_block * A1FS_BLOCK_SIZE);
	int num_dentry = inode->size / sizeof(a1fs_dentry);
	for (a1fs_blk_t i = 0; i < inode->i_extents_count; i++)
	{ // go through each extent
		a1fs_blk_t extent_start = s_extent[i].start;
		int blk_count = s_extent[i].count;

		for (int j = 0; j < blk_count; j++)
		{ //go through each block
			a1fs_dentry *start_dentry = (a1fs_dentry *)(fs->data_blk + (extent_start + j) * A1FS_BLOCK_SIZE);
			for (int dentry_i = 0; dentry_i < A1FS_BLOCK_SIZE / 256; dentry_i++)
			{ //go through each dentry
				if (num_dentry == 0)
				{ //check if gone through all dentries
					return 0;
				}

				if (filler(buf, start_dentry[dentry_i].name, NULL, 0) != 0)
				{
					return -ENOMEM;
				}

				num_dentry -= 1;
			}
		}
	}
	return 0;
}

/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
	mode = mode | S_IFDIR;
	fs_ctx *fs = get_fs();

	//TODO: create a directory at given path with given mode
	char *parent_path;
	char *new_dir;
	if (get_parent_child_str_from_path(&parent_path, &new_dir, path) == -1)
	{
		return -ENOMEM;
	}

	a1fs_ino_t parent_ino_i;

	path_lookup(parent_path, fs, &parent_ino_i);
	a1fs_inode *parent_ino = &(fs->root_ino[parent_ino_i]);
	uint32_t extra_blk;
	if (parent_ino->size % A1FS_BLOCK_SIZE == 0) //data blocks are full
	{
		extra_blk = 1;
	}
	else
	{
		extra_blk = 0;
	}
	// all inodes occupied or all blocks occupied
	// all inodes occupied or all blocks occupied
	if ((fs->sb->s_free_inodes_count == 0) | (fs->sb->s_free_blocks_count < 2 + extra_blk) | (parent_ino->i_extents_count == 512))
	{
		return -ENOSPC;
	}
	a1fs_ino_t child_ino_i = create_inode(fs, mode);
	a1fs_inode *child_inode = &(fs->root_ino[child_ino_i]);
	child_inode->links = 2;
	// add a dentry in the parent inode
	a1fs_dentry dentry = create_dentry(child_ino_i, new_dir);
	add_dentry(parent_ino_i, dentry, fs);
	fs->sb->s_dir_count += 1;
	free(parent_path);
	free(new_dir);

	return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
	fs_ctx *fs = get_fs();

	//TODO: remove the directory at given path (only if it's empty)
	//a1fs_inode child_inode;
	//path_lookup(path, fs, child_inode);
	char *parent_path;
	char *new_dir;
	get_parent_child_str_from_path(&parent_path, &new_dir, path);

	a1fs_ino_t child_ino_i;
	path_lookup(path, fs, &child_ino_i);
	a1fs_ino_t parent_ino_i;
	path_lookup(parent_path, fs, &parent_ino_i);
	a1fs_inode *child_ino = &(fs->root_ino[child_ino_i]);
	if (child_ino->size > 0)
	{ 
		return -ENOTEMPTY;
	}
	a1fs_dentry child_dentry = create_dentry(child_ino_i, new_dir);
	rm_dentry(parent_ino_i, child_dentry, fs);
	unset_bitmap('i', child_ino_i, 1, fs);             //delete the inode
	free(parent_path);
	free(new_dir);
	return 0;
}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi; // unused
	assert(S_ISREG(mode));
	fs_ctx *fs = get_fs();

	//TODO: create a file at given path with given mode
	char *parent_path;
	char *new_file;
	if (get_parent_child_str_from_path(&parent_path, &new_file, path) == -1)
	{
		return -ENOMEM;
	}
	a1fs_ino_t parent_ino_i;
	path_lookup(parent_path, fs, &parent_ino_i);
	a1fs_inode *parent_ino = &(fs->root_ino[parent_ino_i]);
	uint32_t extra_blk;
	if (parent_ino->size % A1FS_BLOCK_SIZE == 0) //data blocks are full
	{
		extra_blk = 1;
	}
	else
	{
		extra_blk = 0;
	}
	// all inodes occupied or all blocks occupied
	if ((fs->sb->s_free_inodes_count == 0) | (fs->sb->s_free_blocks_count < extra_blk) | (parent_ino->i_extents_count == 512))
	{
		return -ENOSPC;
	}
	a1fs_ino_t child_ino_i = create_inode(fs, mode);
	a1fs_inode *child_ino = &(fs->root_ino[child_ino_i]);
	child_ino->links = 1;
	a1fs_dentry child_dentry = create_dentry(child_ino_i, new_file);
	add_dentry(parent_ino_i, child_dentry, fs);
	free(parent_path);
	free(new_file);

	return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
	fs_ctx *fs = get_fs();

	//TODO: remove the file at given path

	char *parent_path;
	char *file;
	get_parent_child_str_from_path(&parent_path, &file, path);
	
	a1fs_ino_t child_ino_i;
	path_lookup(path, fs, &child_ino_i);
    a1fs_inode *inode = &(fs->root_ino[child_ino_i]);
    if(inode->size != 0){
        delete_file_data(child_ino_i, fs); //delete file content
    }
	unset_bitmap('i', child_ino_i, 1, fs); //delete inode
	a1fs_ino_t parent_ino_i;
	path_lookup(parent_path, fs, &parent_ino_i);

	a1fs_dentry child_dentry = create_dentry(child_ino_i, file);
	rm_dentry(parent_ino_i, child_dentry, fs); //rm child dentry
	free(file);
	free(parent_path);
	return 0;


	
}

/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *       Timestamp modifications are not recursive. 
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
	fs_ctx *fs = get_fs();

	//TODO: update the modification timestamp (mtime) in the inode for given
	// path with either the time passed as argument or the current time,
	// according to the utimensat man page

	a1fs_ino_t ino_i;
	path_lookup(path, fs, &ino_i);
	a1fs_inode *inode = &(fs->root_ino[ino_i]);
	if (times == NULL)
	{
		clock_gettime(CLOCK_REALTIME, &(inode->mtime));
	}
	else if (times[1].tv_nsec == UTIME_NOW)
	{
		clock_gettime(CLOCK_REALTIME, &(inode->mtime));
	}
	else if (times[1].tv_nsec == UTIME_OMIT)
	{
		;
	}
	else
	{
		inode->mtime = times[1];
	}

	return 0;
}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
	fs_ctx *fs = get_fs();

	//TODO: set new file size, possibly "zeroing out" the uninitialized range

	a1fs_ino_t ino_i;
	path_lookup(path, fs, &ino_i); // get the inode for the path
	a1fs_inode *inode = &(fs->root_ino[ino_i]);
	uint32_t orig_ino_size = inode ->size;

	// if the input size is larger than the original size of the file, extend the file
	if ((uint32_t)size > inode->size)
	{   
		int offset = size - inode->size;
        int last_fill = (inode->size)%A1FS_BLOCK_SIZE;
        int vacant = A1FS_BLOCK_SIZE - last_fill;
        int deduct = 0;
        int add_ex_blk = 0;
        if(last_fill != 0){
            deduct = vacant;
        }
        if(inode->size == 0){
            add_ex_blk = 1;
        }
        if (((offset>deduct)&&(fs->sb->s_free_blocks_count < divide_ceil(offset - deduct, A1FS_BLOCK_SIZE) + add_ex_blk))|(inode->i_extents_count == 512)){
            return -ENOSPC;
        }

		int error = extend_file(offset, ino_i, fs);
		if (error != 0) {
            // revert back to original via truncate
            truncate_file(ino_i, fs, inode->size - orig_ino_size);
            return error;
		} else {
            clock_gettime(CLOCK_REALTIME, &(inode->mtime));
		    return 0;
		}
	}

	// if the input size is smaller than the original size of the file, truncate the file
	if ((uint32_t)size < inode->size)
	{
		if (size == 0){
		    delete_file_data(ino_i, fs);
		}
		else {
            truncate_file(ino_i, fs, inode->size - size);
		}
	}

    clock_gettime(CLOCK_REALTIME, &(inode->mtime));
	return 0;
}

/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
					 struct fuse_file_info *fi)
{
	(void)fi; // unused
	fs_ctx *fs = get_fs();

	//TODO: read data from the file at given offset into the buffer

	// get the inode
    a1fs_ino_t ino_i;
    path_lookup(path, fs, &ino_i); // get the inode from the path
    a1fs_inode *inode = &(fs->root_ino[ino_i]);
    if(inode->size == 0){// if the file is empty
        buf[0] = '\0';
        return 0;
    }

	// "from somewhere after EOF to somewhere after EOF,
	// you should zero-fill the buffer and return 0"
	if ((uint32_t)offset > inode->size) {
        memset(buf, 0, size);
        buf[size] = '\0';
        return 0;
	}

	// find the start position to read
	unsigned char *start_pos = find_offset(ino_i, fs, offset);

    // "from somewhere before EOF to somewhere before EOF,
    // you should fill the buffer and return the argument size"
    if (offset + size <= inode->size) {
        memcpy(buf, start_pos, size);
        buf[size] = '\0';
        return size;
    } else {    // "from somewhere before EOF to somewhere after EOF,
        // you should fill the buffer with the data before EOF, zero-fill the rest,
        // and return the number of bytes read before EOF"
        unsigned char *end_pos = find_last_blk(ino_i, fs);
        int difference = end_pos - start_pos;
        memcpy(buf, start_pos, difference);
        memset(buf + difference, 0, size - difference); // zero-fill the rest
        buf[size] = '\0';
        return difference;
    }
}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *   ENOSPC  too many extents (a1fs only needs to support 512 extents per file)
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
					  off_t offset, struct fuse_file_info *fi)
{
	(void)fi; // unused
	fs_ctx *fs = get_fs();

    // get the inode
    a1fs_ino_t ino_i;
    path_lookup(path, fs, &ino_i); // get the inode from the path
    a1fs_inode *inode = &(fs->root_ino[ino_i]);

    // write to after EOF == need to extend the file first
    // if extension gives an error then returns error
    int error = extend_file(size + offset - inode->size,ino_i, fs);
	
    if ((offset + size > inode->size) && (error != 0)){return error;}

    // find the start position to write
    unsigned char *start_pos = find_offset(ino_i, fs, offset);
    // start_pos will be in the same blk as the end of writing
    memcpy(start_pos, buf, size);
    clock_gettime(CLOCK_REALTIME, &(inode->mtime));
    return size;
}

static struct fuse_operations a1fs_ops = {
	.destroy = a1fs_destroy,
	.statfs = a1fs_statfs,
	.getattr = a1fs_getattr,
	.readdir = a1fs_readdir,
	.mkdir = a1fs_mkdir,
	.rmdir = a1fs_rmdir,
	.create = a1fs_create,
	.unlink = a1fs_unlink,
	.utimens = a1fs_utimens,
	.truncate = a1fs_truncate,
	.read = a1fs_read,
	.write = a1fs_write,
};

int main(int argc, char *argv[])
{
	a1fs_opts opts = {0}; // defaults are all 0
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	if (!a1fs_opt_parse(&args, &opts))
		return 1;

	fs_ctx fs = {0};
	if (!a1fs_init(&fs, &opts))
	{
		fprintf(stderr, "Failed to mount the file system\n");
		return 1;
	}

	return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
