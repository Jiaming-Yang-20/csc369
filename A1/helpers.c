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
// Helper functions

/**
 * a helper function for path_lookup that finds the inode from dir in file system fs and stores the result in ino_i 
 *
 * @param ino_i     ptr to the inode index in the inode table
 * @param file 	    filename
 * @param fs        a ptr to the file system
 * @return          0 on success; else error
 */
int find_inode_from_dir(a1fs_ino_t *ino_i, char *file, fs_ctx *fs)
{
    a1fs_inode *inode = &(fs->root_ino[*ino_i]);
    if (!(inode->mode & S_IFDIR))
    { //not a dir
        return -ENOTDIR;
    }
    if (inode->size == 0)
    { //dir is empty, i.e. no extents
        return -ENOENT;
    }
    a1fs_blk_t extent_i = inode->s_extent_block;
    int num_dentry = inode->size / sizeof(a1fs_dentry);
    a1fs_extent *s_extent = (a1fs_extent *)(fs->data_blk + extent_i * A1FS_BLOCK_SIZE);
    for (uint32_t i = 0; i < inode->i_extents_count; i++)
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
                    return -ENOENT;
                }
                if (!strcmp(start_dentry[dentry_i].name, file))
                { //if found this file
                    *ino_i = start_dentry[dentry_i].ino;
                    return 0;
                }
                num_dentry -= 1;
            }
        }
    }
    return -ENOENT;
}

/**
 * look up the file/dir from path path in file system fs, once found, stores the inode index of the file into inode_i
 *
 * @param path 	    the path to the file
 * @param fs        a pointer to the file system
 * @param inode_i   stores the inode index of the file
 * @return          0 on success;
 */
int path_lookup(const char *path, fs_ctx *fs, a1fs_ino_t *inode_i)
{
    if (path[0] != '/')
        return -1;
    a1fs_ino_t ino_i = 0; //root inode at inode table index 0
    if (!strcmp(path, "/"))
    {

        *inode_i = ino_i;
        return 0;
    }
    char *string = strdup(&path[1]);
    char *file;
    while ((file = strsep(&string, "/")) != NULL)
    {
        int res = find_inode_from_dir(&ino_i, file, fs);
        if (res != 0)
        { //inode index correspond to this file not found
            return res;
        }
    }
    *inode_i = ino_i;
    return 0;
}

/**
 * get the name path of the parent dir and child file/dir name from the path path
 *
 * @param parent 	stores path of the parent dir
 * @param child     stores name of the child file/dir
 * @param path      path given
 * @return          0 on success; -1 otherwise
 */
int get_parent_child_str_from_path(char **parent, char **child, const char *path)
{
    char *ret = strrchr(path, '/') + 1;
    char *filename;
    char *p_path;
    if ((filename = malloc(strlen(ret) + 1)) == NULL)
    {
        return -1;
    }
    strncpy(filename, ret, strlen(ret));
    filename[strlen(ret)] = '\0';
    int parent_size = strrchr(path, '/') - path;
    if (parent_size == 0)
    {
        parent_size += 1;
    }
    if ((p_path = malloc(parent_size + 1)) == NULL)
    {
        return -1;
    }
    strncpy(p_path, path, parent_size);
    p_path[parent_size] = '\0';
    *parent = p_path;
    *child = filename;
    return 0;
}

/**
 * get an empty inode and set the correpsonding bit in inode bitmap to 1, increase inode count in superblock
 *
 * @param fs        a pointer to the file system
 * @return          index of the empty inode
 */
a1fs_ino_t search_inode_bitmap(fs_ctx *fs)

{
    unsigned char *bitmap = fs->inode_bitmap;

    for (uint32_t byte = 0; byte < fs->sb->s_inodes_count / 8; byte++)
    {
        for (int bit = 0; bit < 8; bit++)
        {
            if (!(bitmap[byte] & (1 << bit)))
            {
                bitmap[byte] |= (1 << bit);
                fs->sb->s_free_inodes_count -= 1;
                return byte * 8 + bit;
            }
        }
    }
    return 0; //won't get here
}

/**
 * create an inode with mode mode in file system fs
 *
 * @param mode 	    the mode of the file
 * @param fs        a pointer to the file system
 * @return          index of the inode
 */
a1fs_ino_t create_inode(fs_ctx *fs, mode_t mode)
{
    a1fs_ino_t inode_i = search_inode_bitmap(fs);
    a1fs_inode *inode = &(fs->root_ino[inode_i]);
    inode->ino_idx = inode_i;
    inode->i_extents_count = 0;
    inode->links = 0;
    inode->mode = mode;
    clock_gettime(CLOCK_REALTIME, &(inode->mtime));
    inode->s_extent_block = 0;
    inode->size = 0;
    return inode_i;
}

/**
 * a ceiling function for x/y
 *
 * @param x 	    numerator
 * @param y         denominator
 * @return          ceil(x/y)
 */
uint32_t divide_ceil(uint32_t x, uint32_t y)
{
    return x / y + ((x % y) != 0);
}

/**
 * flip the bitmap to 1 at index index and of size size
 * free_inodes_count and free_blocks_count are also updated
 *
 * @param map           'i' represents inode bitmap to be flipped; 'd' represents data bitmap
 * @param index         start index of the bits to be flipped
 * @param size          number of bits to be flipped
 * @param fs            pointer to the file system
 */
void set_bitmap(unsigned char map, a1fs_blk_t index, uint32_t size, fs_ctx *fs)
{
    unsigned char *bitmap;
    if (map == 'i') //inode bitmap
    {
        bitmap = fs->inode_bitmap;
        fs->sb->s_free_inodes_count -= size;
    }
    else
    {
        bitmap = fs->block_bitmap; //block bitmap
        fs->sb->s_free_blocks_count -= size;
    }
    for (uint32_t i = index; i < index + size; i++)
    {
        int byte = i / 8;
        int bit = i % 8;
        bitmap[byte] |= (1 << bit);
    }
}

/**
 * flip the bitmap to 0 at index index and of size size
 * free_inodes_count and free_blocks_count are also updated
 *
 * @param map           'i' represents inode bitmap to be flipped; 'd' represents data bitmap
 * @param index         start index of the bits to be flipped
 * @param size          number of bits to be flipped
 * @param fs            pointer to the file system
 */
void unset_bitmap(unsigned char map, uint32_t index, uint32_t size, fs_ctx *fs)
{
    unsigned char *bitmap;
    if (map == 'i') //inode bitmap
    {
        bitmap = fs->inode_bitmap;
        fs->sb->s_free_inodes_count += size;
    }
    else
    {
        bitmap = fs->block_bitmap; //block bitmap
        fs->sb->s_free_blocks_count += size;
    }
    for (uint32_t i = index; i < index + size; i++)
    {
        int byte = i / 8;
        int bit = i % 8;
        bitmap[byte] &= (~(1 << bit));
    }
}

/**
 * precondition: file system has enough number of free blks left
 * search from the beginning for empty contiguous blocks of size size in file system fs
 * If cannot find one, store the largest number of contiguous blocks in fs into extent
 * Flip the bits of these empty contiguous blocks and decrease the free_blk count
 *
 * @param size          number of contiguous blocks to look for
 * @param fs            pointer to the file system
 * @param extent        pointer to an extent that stores the resulting extent.start and extent.count
 */
void search_blk_bitmap(uint32_t size, fs_ctx *fs, a1fs_extent *extent)
{                                                                                    // 0010 0011
    unsigned char *bitmap = fs->block_bitmap;                                        //get the blk bitmap
    uint32_t num_data_blk = fs->sb->s_blocks_count - fs->sb->s_first_data_block + 1; //total num of datablks in the fs
    int bit_iterated = 0;                                                            //num of bits iterated in bitmap
    int end_bit;                                                                     // num of bit to iterate in the byte
    extent->count = 0;                                                               //start of the largest cts blk
    extent->start = 0;
    uint32_t count = 0;
    uint32_t start = 0;
    for (uint32_t byte = 0; byte < divide_ceil(num_data_blk, 8); byte++)
    {
        if (num_data_blk - bit_iterated >= 8)
        {
            end_bit = 8;
        }
        else
        {
            end_bit = num_data_blk % 8;
        }
        for (int bit = 0; bit < end_bit; bit++)
        {
            bit_iterated += 1;
            if ((bitmap[byte] & (1 << bit)) == 0)
            {
                count += 1;
                if (size == count)
                {
                    extent->start = start;
                    extent->count = count;
                    set_bitmap('d', extent->start, extent->count, fs);
                    return;
                }
            }
            else
            {
                if (count > extent->count)
                {
                    extent->count = count;
                    extent->start = start;
                }
                start = bit_iterated;
                count = 0;
            }
        }
    }
    set_bitmap('d', extent->count, extent->start, fs);
}

/**
 * search at preferred_start_index for empty contiguous blocks of size size in file system fs
 * if success, flip the bits of these empty contiguous blocks and decrease the free_blk count
 *
 * @param preferred_start_index     starting point of the search
 * @param size                      number of contiguous blocks to look for
 * @param fs                        pointer to the file system
 * @return                          0 on success; -1 if not found
 */
int search_blk_bitmap_at_idx(a1fs_blk_t preferred_start_index, uint32_t size, fs_ctx *fs)
{
    unsigned char *bitmap = fs->block_bitmap;
    uint32_t num_data_blk = fs->sb->s_blocks_count - fs->sb->s_first_data_block + 1;
    if (preferred_start_index + size > num_data_blk)
    {
        return -1;
    }

    for (uint32_t idx = preferred_start_index; idx < preferred_start_index + size; idx++)
    {
        int byte = idx / 8;
        int bit = idx % 8;
        if ((bitmap[byte] & (1 << bit)) == 1)
        {
            return -1;
        }
    }
    set_bitmap('d', preferred_start_index, size, fs);
    return 0;
}

/**
 * write extent extent to the extent block of the file at inode index ino_i in file system fs
 *
 * @param ino_i     inode index of the file
 * @param extent    an extent struct to be written to the extent block of the file
 * @param fs        a pointer to the file system
 * @return          0 on success; -1 otherwise
 */
int write_extent(a1fs_ino_t ino_i, a1fs_extent extent, fs_ctx *fs)
{
    a1fs_inode *ino = &(fs->root_ino[ino_i]);
    if(ino->i_extents_count == 512){
        return -1;
    }
    a1fs_blk_t extent_blk = ino->s_extent_block;
    a1fs_extent *ptr = (a1fs_extent *)(fs->data_blk + extent_blk * A1FS_BLOCK_SIZE);
    memcpy(&(ptr[ino->i_extents_count]), &extent, sizeof(a1fs_extent));
    ino->i_extents_count += 1;
    return 0;
}

/**
 * allocate blocks to write the dentry for dir at inode index dir_i in file system fs
 *
 * @param dir_i     inode index of the dir
 * @param fs        a pointer to the file system
 * @return          index of the new blk to write the dentry
 */
a1fs_blk_t allocate_blks_for_dir(a1fs_ino_t dir_i, fs_ctx *fs)
{
    a1fs_inode *parent_dir = &(fs->root_ino[dir_i]);
    if (parent_dir->size == 0)
    { // parent is empty, need to allocate an extent blk
        a1fs_extent extent;
        search_blk_bitmap(1, fs, &extent);
        a1fs_blk_t extent_blk = extent.start;    //get an extent blk
        parent_dir->s_extent_block = extent_blk; //connect extent blk to parent_dir
        search_blk_bitmap(1, fs, &extent);       //get an extent
        write_extent(dir_i, extent, fs);         //write this extent to parent_dir
        return extent.start;
    }
    else
    { //parent directory has extents
        a1fs_extent *first_extent = (a1fs_extent *)(fs->data_blk + parent_dir->s_extent_block * A1FS_BLOCK_SIZE);
        a1fs_extent *last_extent = &(first_extent[parent_dir->i_extents_count - 1]); //get the last extent
        a1fs_blk_t end_db = last_extent->start + last_extent->count - 1;             //get the last dlk
        if (search_blk_bitmap_at_idx(end_db + 1, 1, fs) != -1)
        { // see if can extend last extent
            last_extent->count += 1;
            return end_db + 1;
        }
        else
        {
            a1fs_extent extent;
            search_blk_bitmap(1, fs, &extent); //get a new extent
            write_extent(dir_i, extent, fs);   // write this extent to parent dir
            return extent.start;
        }
    }
}

/**
 * write a dentry struct dentry to the datablk blk of the dir at inode index dir_i in file system fs
 * increase links in dir and update mtime
 *
 * @param dir_i     inode index of the dir
 * @param blk       index of the data block of the dir
 * @param index     index for the dentry to be written to
 * @param dentry    the dentry struct to be written into blk
 * @param fs        a pointer to the file system
 */
void write_dentry(a1fs_ino_t dir_i, a1fs_blk_t blk, int index, a1fs_dentry dentry, fs_ctx *fs)
{
    a1fs_inode *dir = &(fs->root_ino[dir_i]);
    a1fs_dentry *ptr = (a1fs_dentry *)(fs->data_blk + blk * A1FS_BLOCK_SIZE);
    memcpy(&(ptr[index]), &dentry, sizeof(a1fs_dentry));
    dir->size += sizeof(a1fs_dentry);
    dir->links += 1;
    clock_gettime(CLOCK_REALTIME, &(dir->mtime));
}

/**
 * get the blk_idx and the dentry index of the last dentry in dir to write to at inode index dir_i in file system fs
 * assume don't need a new blk
 *
 * @param dir_i     inode index of the dir
 * @param blk       index of the data block of the dir
 * @param index     stores the insertion point
 * @param fs        a pointer to the file system
 */
void get_dentry_insertion_point(a1fs_ino_t dir_i, a1fs_blk_t *blk, int *index, fs_ctx *fs)
{
    a1fs_inode *dir = &(fs->root_ino[dir_i]);
    *index = (dir->size / sizeof(a1fs_dentry)) % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry));
    a1fs_extent *first_extent = (a1fs_extent *)(fs->data_blk + dir->s_extent_block * A1FS_BLOCK_SIZE);
    a1fs_extent last_extent = first_extent[dir->i_extents_count - 1];
    a1fs_blk_t end_db = last_extent.start + last_extent.count - 1;
    *blk = end_db;
}

/**
 * add the dentry dentry to dir at inode index dir_i in file system fs
 *
 * @param dir_i     inode index of the dir
 * @param dentry    the dentry struct to be added
 * @param fs        a pointer to the file system
 */
void add_dentry(a1fs_ino_t dir_i, a1fs_dentry dentry, fs_ctx *fs)
{
    a1fs_inode *dir = &(fs->root_ino[dir_i]);
   
    if (dir->size % A1FS_BLOCK_SIZE == 0)
    { //need to allocate blocks
        a1fs_blk_t blk_to_write = allocate_blks_for_dir(dir_i, fs);
        write_dentry(dir_i, blk_to_write, 0, dentry, fs); // at index 0 since the blk is newly allocated
    }
    else
    { // add to the end of the dir
        a1fs_blk_t blk;
        int index;
        get_dentry_insertion_point(dir_i, &blk, &index, fs);
        write_dentry(dir_i, blk, index, dentry, fs);
    }
}

/**
 * get the last dentry in dir at inode index dir_i in file system fs
 *
 * @param dir_i      inode index of the dir
 * @param fs         a pointer to the file system
 * @return           the last dentry struct in the dir
 */
a1fs_dentry get_last_dentry(a1fs_ino_t dir_i, fs_ctx *fs)
{
    a1fs_blk_t blk;
    int index;
    get_dentry_insertion_point(dir_i, &blk, &index, fs);
    index -= 1;
    a1fs_dentry *first_dentry = (a1fs_dentry *)(fs->data_blk + blk * A1FS_BLOCK_SIZE);
    return first_dentry[index];
}

/**
 * replace a dentry in dir at inode index dir_i with name name with new_dentry in file system fs
 *
 * @param dir_i         inode index of the dir
 * @param name          name of the dentry to be replaced
 * @param new_dentry    new dentry
 * @param fs            a pointer to the file system
 */
void replace_dentry(a1fs_ino_t dir_i, char *name, a1fs_dentry new_dentry, fs_ctx *fs)
{
    a1fs_inode *inode = &(fs->root_ino[dir_i]);
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
                    return;
                }

                if (strcmp(start_dentry[dentry_i].name, name) == 0)
                {
                    (&(start_dentry[dentry_i]))->ino = new_dentry.ino;
                    strncpy((&(start_dentry[dentry_i]))->name, new_dentry.name, A1FS_NAME_MAX-1);
                    ((&(start_dentry[dentry_i]))->name)[A1FS_NAME_MAX-1] = '\0';
                    return;
                }

                num_dentry -= 1;
            }
        }
    }
}

/**
 * remove the last dentry in dir at inode index dir_i in file system fs
 *
 * @param dir_i         inode index of the dir
 * @param fs            a pointer to the file system
 */
void rm_last_dentry(a1fs_ino_t dir_i, fs_ctx *fs)
{
    a1fs_inode *dir = &(fs->root_ino[dir_i]);
    int last_dentry_i = (dir->size / sizeof(a1fs_dentry)) % (A1FS_BLOCK_SIZE / sizeof(a1fs_dentry)) - 1;
    a1fs_extent *first_extent = (a1fs_extent *)(fs->data_blk + dir->s_extent_block * A1FS_BLOCK_SIZE);
    a1fs_extent last_extent = first_extent[dir->i_extents_count - 1];
    a1fs_blk_t end_db = last_extent.start + last_extent.count - 1;
    if (last_dentry_i == 0)
    {
        unset_bitmap('d', end_db, 1, fs);
        (&last_extent)->count -= 1;
    }
    if ((&last_extent)->count == 0)
    {
        dir->i_extents_count -= 1;
    }
    dir->size -= sizeof(a1fs_dentry); //delete the dentry
    dir->links -= 1;
    clock_gettime(CLOCK_REALTIME, &(dir->mtime));
}

/**
 * remove a dentry struct dentry in dir at inode index dir_i in file system fs
 *
 * @param dir_i             inode index of the dir
 * @param dentry            dentry struct to be removed
 * @param fs                a pointer to the file system
 */
void rm_dentry(a1fs_ino_t dir_i, a1fs_dentry dentry, fs_ctx *fs)
{   a1fs_inode *dir = &(fs->root_ino[dir_i]);
    a1fs_dentry last_dentry = get_last_dentry(dir_i, fs);
    if ((strcmp(dentry.name, last_dentry.name)) != 0)
    {
        replace_dentry(dir_i, dentry.name, last_dentry, fs);
    }
    rm_last_dentry(dir_i, fs);
    if(dir->size == 0){ //deallocate extent blk
        unset_bitmap('d', dir->s_extent_block, 1, fs);
    }

}

/**
 * create a dentry struct specified by inode index ino and name name
 *
 * @param ino               inode index of the file/dir
 * @param name              name of the file/dir
 * @return                  a dentry struct specified by ino and name
 */
a1fs_dentry create_dentry(a1fs_ino_t ino, char *name)
{
    a1fs_dentry dentry = {0};
    dentry.ino = ino;
    strncpy(dentry.name, name, A1FS_NAME_MAX - 1);
    dentry.name[A1FS_NAME_MAX - 1] = '\0';
    return dentry;
}


/**
 * create an extent struct specified by start index start and number of blks count
 *
 * @param start             start index of the extent
 * @param count             number of blks in the extent
 * @return                  an extent struct specified by start and count
 */
a1fs_extent create_extent(a1fs_blk_t start, uint32_t count)
{
    a1fs_extent extent;
    extent.start = start;
    extent.count = count;
    return extent;
}

/**
 * find the pointer to the byte specified by offset of the file with inode index ino in file system fs
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @param offset            offset of the file
 * @return                  pointer to the byte
 */
unsigned char *find_offset(a1fs_ino_t ino, fs_ctx *fs, uint32_t offset)
{
    a1fs_inode *inode = &(fs->root_ino[ino]);   // get the inode
    a1fs_blk_t extent_blk = inode->s_extent_block;  // get the extent block
    a1fs_extent *curr_extent = (a1fs_extent *)(fs->data_blk + extent_blk * A1FS_BLOCK_SIZE);
    int block_offset = offset / A1FS_BLOCK_SIZE;
    int remaining_data = offset % A1FS_BLOCK_SIZE;
    int num_extent = 0;
    int num_block_in_extent = curr_extent->count;

    while (block_offset - num_block_in_extent > 0) {
        block_offset -= num_block_in_extent;
        num_extent ++;
        curr_extent = (a1fs_extent *)(curr_extent + num_extent * sizeof(a1fs_extent));
        num_block_in_extent = curr_extent->count;
    }   // when the loop terminates, curr_extent is the correct extent containing block_offset

    return (fs->data_blk + curr_extent->start * A1FS_BLOCK_SIZE + remaining_data);

}

/**
 * delete all the data of the file with inode index ino in file system fs
 * assuming bytes_to_delete > 0
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 */
void delete_file_data(a1fs_ino_t ino, fs_ctx *fs)
{
    a1fs_inode *inode = &(fs->root_ino[ino]);   // get the inode
    a1fs_extent *first_extent = (a1fs_extent *)(fs->data_blk + inode->s_extent_block * A1FS_BLOCK_SIZE);
    for (int extent_idx = 0; (uint32_t)extent_idx < inode->i_extents_count; extent_idx++)
    {
        a1fs_extent *curr_extent = &(first_extent[extent_idx]); //get the next extent
        unset_bitmap('d', curr_extent->start, curr_extent->count, fs);
    }
    unset_bitmap('d', inode->s_extent_block, 1, fs); // unset the bit for extent blk
    inode->size = 0;
}

/**
 * truncate the file with inode index ino in the file system fs by bytes_to_delete bytes
 * assuming bytes_to_delete > 0
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @param bytes_to_delete   bytes to truncate
 */
void truncate_file(a1fs_ino_t ino, fs_ctx *fs, int bytes_to_delete)
{
    a1fs_inode *inode = &(fs->root_ino[ino]);   // get the inode
    a1fs_extent *first_extent = (a1fs_extent *)(fs->data_blk + inode->s_extent_block * A1FS_BLOCK_SIZE);
    a1fs_extent *last_extent = first_extent + (inode->i_extents_count - 1) * sizeof(a1fs_extent); //get the last extent
    a1fs_blk_t last_data_block = last_extent->start + last_extent->count - 1;

    int remainder = inode->size % A1FS_BLOCK_SIZE;

    // if file size is not multiple of A1FS_BLOCK_SIZE, truncate the last blk accordingly
    if (remainder != 0)
    {
        // if bytes_to_delete is within the last data block, nothing changes to the extent
        if (bytes_to_delete < remainder) {
            inode->size -= bytes_to_delete;
            return ;
        }

        // bytes_to_delete is beyond the last data block, delete the last data blk first
        unset_bitmap('d', last_data_block, 1, fs);

        inode->size -= remainder;
        bytes_to_delete -= remainder;
        last_extent->count--;
        // if this last extent only has one data block, remove this extent, update last_data_block
        if (last_extent->count == 0) {inode->i_extents_count--;}
    }

    // from now on, the remainder at the end has been truncated

    while (bytes_to_delete >= A1FS_BLOCK_SIZE) {
        last_extent = first_extent + (inode->i_extents_count - 1) * sizeof(a1fs_extent); //get the current last extent
        last_data_block = last_extent->start + last_extent->count - 1;  // get the current last data blk
        unset_bitmap('d', last_data_block, 1, fs);
        inode->size -= A1FS_BLOCK_SIZE;
        bytes_to_delete -= A1FS_BLOCK_SIZE;
        last_extent->count--;
        if (last_extent->count == 0) {inode->i_extents_count--;}
    }

    // we have reached the last data blk to delete, nothing changes to the extent
    inode->size -= bytes_to_delete;
    return ;
}

/**
 * get the pointer to the last blk of the file with inode index ino in file system fs
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @return                  the pointer to the last blk
 */
unsigned char *find_last_blk(a1fs_ino_t ino, fs_ctx *fs) {
    a1fs_inode *inode = &(fs->root_ino[ino]);   // get the inode
    a1fs_extent *first_extent = (a1fs_extent *) (fs->data_blk + inode->s_extent_block * A1FS_BLOCK_SIZE);
    a1fs_extent *last_extent = &(first_extent[inode->i_extents_count - 1]); //get the last extent
    unsigned char *first_blk = fs->data_blk + last_extent->start * A1FS_BLOCK_SIZE; // first blk of the last extent
    unsigned char *last_blk = first_blk + last_extent->count * A1FS_BLOCK_SIZE; // last blk of the last extent

    return last_blk;
}

/**
 * write length 0s to the blk in file with inode index ino_i in file system fs from start (must fill in the single blk)
 *
 * @param ino_i             inode index of the file
 * @param blk               index of the data block
 * @param start             start of writing
 * @param length            length of writing
 * @param fs                a pointer to the file system
 */
void write_zero_to_blk(a1fs_ino_t ino_i, a1fs_blk_t blk, int start, int length, fs_ctx *fs){
    a1fs_inode* inode = &(fs->root_ino[ino_i]);
    inode -> size += length;
    memset(fs->data_blk+ blk*A1FS_BLOCK_SIZE + start, 0, length);
}

/**
 * populate size zeros after the last extent of the file with inode index ino_i in the file system fs
 *
 * @param ino_i             inode index of the file
 * @param size              bytes to extend
 * @param fs                a pointer to the file system
 * @return                  0 on success; -errno on error.
 */
int populate_extent_blk(a1fs_ino_t ino_i, uint32_t size, fs_ctx *fs){
    uint32_t num_db_needed = divide_ceil(size, A1FS_BLOCK_SIZE);
    uint32_t size_remain = size;
    while (num_db_needed != 0){
        a1fs_extent extent;
        search_blk_bitmap(num_db_needed, fs, &extent);
        if(write_extent(ino_i, extent, fs) == -1){
            return -ENOSPC;
        }
        num_db_needed -= extent.count;
        if(size_remain > extent.count * A1FS_BLOCK_SIZE){
            write_zero_to_blk(ino_i, extent.start,0, extent.count * A1FS_BLOCK_SIZE, fs );
            size_remain -= extent.count * A1FS_BLOCK_SIZE;
        }else{
            write_zero_to_blk(ino_i, extent.start,0, size_remain, fs );
            return 0;
        }
    }
    return 0;

}

/**
 * get the index of the last data blk of the file with inode index ino in file system fs
 *
 * @param ino               inode index of the file
 * @param fs                a pointer to the file system
 * @return                  the index of the last blk
 */
a1fs_blk_t get_last_blk(a1fs_ino_t ino, fs_ctx *fs) {
    a1fs_inode *inode = &(fs->root_ino[ino]);   // get the inode
    a1fs_extent *first_extent = (a1fs_extent *) (fs->data_blk + inode->s_extent_block * A1FS_BLOCK_SIZE);
    a1fs_extent last_extent = first_extent[inode->i_extents_count - 1]; //get the last extent
    return last_extent.start + last_extent.count - 1;
}

/**
 * extend the file by filling extend_size 0s at the end of the file with inode index ino_i in file system fs
 *
 * @param extend_size 	    bytes to extend
 * @param ino_i             inode index of the file
 * @param fs                a pointer to the file system
 * @return                  0 on success; -errno on error.
 */
int extend_file(uint32_t extend_size, a1fs_ino_t ino_i, fs_ctx *fs){
    uint32_t offset_remain = extend_size;
    a1fs_inode* inode = &(fs->root_ino[ino_i]);
    if(inode->size == 0){ // if file is empty
        a1fs_extent extent;
        search_blk_bitmap(1, fs, &extent);
        inode->s_extent_block = extent.start;
        if(populate_extent_blk(ino_i, offset_remain, fs)== -ENOSPC){
                return -ENOSPC;
        }
        return 0;
    }else{ // if file is not empty 
        a1fs_blk_t last_blk = get_last_blk(ino_i, fs);
        // fill the remaining block
        if(inode->size % A1FS_BLOCK_SIZE != 0){ // if the last datablk is not fully filled
            
            int last_fill = (inode->size)%A1FS_BLOCK_SIZE;
            if(last_fill + extend_size <= A1FS_BLOCK_SIZE){ // data doesn't extend last data blk
                write_zero_to_blk(ino_i, last_blk,last_fill, extend_size, fs );
                return 0;

            }else{ //data exceeds last data blk
                write_zero_to_blk(ino_i, last_blk,last_fill, A1FS_BLOCK_SIZE - last_fill, fs );
                offset_remain -= A1FS_BLOCK_SIZE - last_fill;
            }
        }
        // write the remaining blks
        uint32_t num_db_needed = divide_ceil(offset_remain, A1FS_BLOCK_SIZE);
        if(search_blk_bitmap_at_idx(last_blk, offset_remain, fs)==0){ // can extend the previous extent
            write_zero_to_blk(ino_i, last_blk + 1, 0, offset_remain, fs );
            a1fs_extent *first_extent = (a1fs_extent *) (fs->data_blk + inode->s_extent_block * A1FS_BLOCK_SIZE);
            a1fs_extent *last_extent = &(first_extent[inode->i_extents_count - 1]);
            last_extent->count += num_db_needed;
            return 0;

        }else{
            if(populate_extent_blk(ino_i, offset_remain, fs)== -ENOSPC){
                return -ENOSPC;
            }
        }
    }
    return 0;
}
    