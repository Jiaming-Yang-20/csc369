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
 * CSC369 Assignment 1 - a1fs formatting tool.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "a1fs.h"
#include "map.h"
#include "time.h"
#include "util.h"

/** Command line options. */
typedef struct mkfs_opts
{
	/** File system image file path. */
	const char *img_path;
	/** Number of inodes. */
	size_t n_inodes;

	/** Print help and exit. */
	bool help;
	/** Overwrite existing file system. */
	bool force;
	/** Zero out image contents. */
	bool zero;

} mkfs_opts;

static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
	fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}

static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
	char o;
	while ((o = getopt(argc, argv, "i:hfvz")) != -1)
	{
		switch (o)
		{
		case 'i':
			opts->n_inodes = strtoul(optarg, NULL, 10);
			break;

		case 'h':
			opts->help = true;
			return true; // skip other arguments
		case 'f':
			opts->force = true;
			break;
		case 'z':
			opts->zero = true;
			break;

		case '?':
			return false;
		default:
			assert(false);
		}
	}

	if (optind >= argc)
	{
		fprintf(stderr, "Missing image path\n");
		return false;
	}
	opts->img_path = argv[optind];

	if (opts->n_inodes == 0)
	{
		fprintf(stderr, "Missing or invalid number of inodes\n");
		return false;
	}
	return true;
}

/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
	//TODO: check if the image already contains a valid a1fs superblock
	return ((a1fs_superblock *)(image + A1FS_BLOCK_SIZE))->magic == A1FS_MAGIC;
}

/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
	//TODO: initialize the superblock and create an empty root directory
	//NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777
	if (opts->n_inodes < 1) return false; // n_inode should be at least 1
	
	int num_blk_inode_bitmap = pos_ceil(opts->n_inodes, 8 * A1FS_BLOCK_SIZE);
	//num of blocks needed for inode bitmap
	int num_blk_inode_table = pos_ceil(opts->n_inodes * sizeof(a1fs_inode), A1FS_BLOCK_SIZE);
	//num of blocks needed for inode table
	int min_num_blks = 2 + num_blk_inode_bitmap + num_blk_inode_table;
	if (size < (size_t)min_num_blks) return false;

	a1fs_superblock sb;
	sb.magic = A1FS_MAGIC;
	sb.size = size;
	sb.s_inodes_count = opts->n_inodes;
	sb.s_blocks_count = size / A1FS_BLOCK_SIZE - 1;
	sb.s_dir_count = 1;

	sb.s_free_inodes_count = opts->n_inodes - 1; //minus root inode
	sb.s_inode_bitmap = 2;
	//bitmap start at block 2
	int num_data_block = sb.s_blocks_count - 1 - num_blk_inode_bitmap - num_blk_inode_table;
	// num of blks included in free block bitmap
	int num_blk_bitmap = pos_ceil(num_data_block, 8 * A1FS_BLOCK_SIZE);
	//num of blks needed for free block bitmap
	sb.s_block_bitmap = 2 + num_blk_bitmap;
	sb.s_first_inode_block = sb.s_block_bitmap + num_blk_bitmap;
	sb.s_first_data_block = sb.s_first_inode_block + num_blk_inode_table;
	sb.s_free_blocks_count = sb.s_blocks_count - sb.s_first_data_block + 1;
	memcpy(image + A1FS_BLOCK_SIZE, &sb, sizeof(a1fs_superblock));
	unsigned char *inode_bitmap = image + sb.s_inode_bitmap * A1FS_BLOCK_SIZE;
	unsigned char *data_bitmap = image + sb.s_block_bitmap * A1FS_BLOCK_SIZE;

	memset(inode_bitmap, 0, num_blk_inode_bitmap * A1FS_BLOCK_SIZE);
	memset(data_bitmap, 0, num_blk_bitmap * A1FS_BLOCK_SIZE);
	inode_bitmap[0] = 1;		   // = 0000 0001
	data_bitmap[0] = 0; // = 0000 0000
	a1fs_inode root = {0};
	root.mode = S_IFDIR | 0777;
	root.links = 2;
	root.size = 0;
	clock_gettime(CLOCK_REALTIME, &(root.mtime));
	root.s_extent_block = 0;
	root.i_extents_count = 0;
	root.ino_idx = 0;
	memcpy(image + sb.s_first_inode_block * A1FS_BLOCK_SIZE, &root, sizeof(a1fs_inode));

	return true;
}

int main(int argc, char *argv[])
{
	mkfs_opts opts = {0}; // defaults are all 0
	if (!parse_args(argc, argv, &opts))
	{
		// Invalid arguments, print help to stderr
		print_help(stderr, argv[0]);
		return 1;
	}
	if (opts.help)
	{
		// Help requested, print it to stdout
		print_help(stdout, argv[0]);
		return 0;
	}

	// Map image file into memory
	size_t size;
	void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
	if (image == NULL)
		return 1;

	// Check if overwriting existing file system
	int ret = 1;
	if (!opts.force && a1fs_is_present(image))
	{
		fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
		goto end;
	}

	if (opts.zero)
		memset(image, 0, size);
	if (!mkfs(image, size, &opts))
	{
		fprintf(stderr, "Failed to format the image\n");
		goto end;
	}

	ret = 0;
end:
	munmap(image, size);
	return ret;
}
