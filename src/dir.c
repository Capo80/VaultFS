#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "ransomfs.h"

// recursive function to read and extent tree of a directory find some free space and  add a filename
// TODO every fucntion that reads the exent tree is the same, is there a better way to do this?
// return 0 if successful
int add_file_to_directory(struct super_block* sb, struct ransomfs_extent_header *block_head, const unsigned char* filename, uint32_t ino, uint8_t file_type) {

    int i = 0, j = 0, off = 0;
    struct buffer_head *bh = NULL;

    printk(KERN_INFO "Started the read of the extent tree for add file\n");

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        struct ransomfs_extent* curr_leaf;
        struct ransomfs_dir_record* curr_record;

        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {
            curr_leaf = (struct ransomfs_extent*) &block_head[i+1];
            
            printk(KERN_INFO "Reading entry %d\n", i);

            printk(KERN_INFO "Start data block: %d - len: %d\n", curr_leaf->data_block, curr_leaf->len);
                
            //iterate over data block of single extent
            for (j = curr_leaf->data_block; j < curr_leaf->data_block + curr_leaf->len; j++) {     
                
                bh = sb_bread(sb, j);
                if (!bh)
                    return -EIO;

                curr_record = (struct ransomfs_dir_record*) bh->b_data;
                //iterate over records of single block
                while (off < RANSOMFS_BLOCK_SIZE) {
                    //async add file to folder
                    if (curr_record->ino == 0)
                        if (__sync_bool_compare_and_swap(&curr_record->ino,0, ino)) {
                            curr_record->name_len = strlen(filename);
                            memcpy(curr_record->filename, filename, curr_record->name_len+1);
                            curr_record->file_type = file_type;
                            mark_buffer_dirty(bh);
                            brelse(bh);
                            goto exit;
                        }
                    curr_record++;
                    off += sizeof(struct ransomfs_dir_record);
                }

                brelse(bh);
           }
        }

    } else {
        //tree has depth zero call this function again at lower levels
        struct ransomfs_extent_idx* curr_node;
        for (i = 0; i < block_head->entries; i++) {
            //int ret = 0;
            curr_node = ((struct ransomfs_extent_idx*) block_head) + i + 1;
            //TODO compelete this
        }

    }

exit:
    return 0;
}


//recursive function to read and extent tree that keeps track of a direcotry data blocks
// TODO need some concurrency checks here? probrably not considering files cannot be deleted
static int read_dir_extent_tree(struct super_block* sb, struct dir_context *ctx, struct ransomfs_extent_header *block_head) {

    uint16_t i = 0;
    uint32_t j = 0;
    int off = 0, file_counter = 0;
    struct buffer_head *bh = NULL;
    struct ransomfs_extent* curr_leaf;
    struct ransomfs_dir_record* curr_record;
    
    printk(KERN_INFO "Started the read of the extent tree\n");

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {

            //printk(KERN_INFO "Reading entry %d %p %p %p %u\n", i, block_head, curr_leaf, &block_head[1], block_head[1].max);
            
            curr_leaf = (struct ransomfs_extent*) &block_head[i+1];
            
            printk(KERN_INFO "Reading entry %d\n", i);
            
            //for (ii = 0; ii < 40; ii++) {
            //    printk(KERN_INFO "%x\n", ((short*)block_head)[ii]);
            //}
            printk(KERN_INFO "Start data block: %u - len: %u\n", curr_leaf->data_block, curr_leaf->len);

            //iterate over data block of single extent
            for (j = curr_leaf->data_block; j < curr_leaf->data_block + curr_leaf->len; j++) {     
                
                bh = sb_bread(sb, j);
                if (!bh)
                    return -EIO;

                curr_record = (struct ransomfs_dir_record*) bh->b_data;
                //iterate over records of single block
                while (off < RANSOMFS_BLOCK_SIZE) {
                    if (curr_record->ino != 0) {
                        if (file_counter >= ctx->pos-2) {
                            if (!dir_emit(ctx, curr_record->filename, RANSOMFS_MAX_FILENAME, curr_record->ino, curr_record->file_type)) {
                                brelse(bh);
                                return 0; //failed to emit
                            }
                            ctx->pos++;
                        }
                        file_counter++;
                    }
                    curr_record++;
                    off += sizeof(struct ransomfs_dir_record);
                }

                brelse(bh);
           }
        }

    } else {
        //tree has depth zero call this function again at lower levels
        struct ransomfs_extent_idx* curr_node;
        for (i = 0; i < block_head->entries; i++) {
            //int ret = 0;
            curr_node = (struct ransomfs_extent_idx*) block_head + i + 1;
            //TODO compelete this
        }

    }

    return file_counter;
}

/*
	Iterate over the files in the folder and pass them to the context.
	This function is called multiple times by the VFS when its changing the ctx pos
*/
static int ransomfs_iterate(struct file *dir, struct dir_context *ctx)
{
    struct inode *inode = file_inode(dir);
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(inode);
    struct super_block *sb = inode->i_sb;

    printk(KERN_INFO "iterate called, pos: %lld\n", ctx->pos) ;
    
    // Check that dir is a directory
    if (!S_ISDIR(inode->i_mode))
        return -ENOTDIR;
    
    //check that we are within the limits 
    if (ctx->pos >= RANSOMFS_MAX_FOLDER_FILES)
        return 0;
    
    // . and ..
    if (ctx->pos < 2)
        if (!dir_emit_dots(dir, ctx)) //FIXME: not working, why?
	        return 0;

    //iterate over data blocks with extents
    return read_dir_extent_tree(sb, ctx, ci->extent_tree);
}

const struct file_operations ransomfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = ransomfs_iterate,
};