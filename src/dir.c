#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "ransomfs.h"

//recursive function to read and extent tree that keeps track of a direcotry data blocks
// TODO need some concurrency checks here? probrably not considering files cannot be deleted
static int read_dir_extent_tree(struct super_block* sb, struct dir_context *ctx, struct ransomfs_extent_header *block_head) {

    int i = 0, j = 0, off = 0;
    struct buffer_head *bh = NULL;

    printk(KERN_INFO "Started the read of the extent tree\n");

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        struct ransomfs_extent* curr_leaf;
        struct ransomfs_dir_record* curr_record;


        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {
            curr_leaf = (struct ransomfs_extent*) block_head + i + 1;
            
            printk(KERN_INFO "Reding entry %d\n", i);

            printk(KERN_INFO "Start data block: %d - len: %d\n", curr_leaf->data_block, curr_leaf->len);
                
            //iterate over data block of single extent
            for (j = curr_leaf->data_block; j < curr_leaf->data_block + curr_leaf->len; j++) {     
                
                bh = sb_bread(sb, j);
                if (!bh)
                    return -EIO;

                curr_record = (struct ransomfs_dir_record*) bh->b_data;
                //iterate over records of single block
                while (off < RANSOMFS_BLOCK_SIZE) {
                    if (curr_record->ino != 0)
                        if (!dir_emit(ctx, curr_record->filename, RANSOMFS_MAX_FILENAME, curr_record->ino, curr_record->file_type))
                            return 0; //failed to emit
                    
                    curr_record++;
                    off += sizeof(struct ransomfs_dir_record);
                    ctx->pos++;
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

    return 0;
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
    
    // . and ..
    if (ctx->pos < 2)
	    if (!dir_emit_dots(dir, ctx)) //FIXME: not working, why?
	        return 0;
    
    //check that we are within the limits 
    if (ctx->pos >= RANSOMFS_MAX_FOLDER_FILES + 2)
        return 0;
    
    //iterate over data blocks with extents
    return read_dir_extent_tree(sb, ctx, ci->extent_tree);
}

const struct file_operations ransomfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = ransomfs_iterate,
};