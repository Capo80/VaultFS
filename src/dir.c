#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "ransomfs.h"

//adds a file to a dir record block
// 1 on success, 0 on failure, negative on error
int add_file_to_dir_record(struct super_block* sb, uint32_t block_no, const unsigned char* filename, uint32_t ino, uint8_t file_type) {
    
    struct ransomfs_dir_record* dir_record;
    int off = 0;
    struct buffer_head* bh = sb_bread(sb, block_no);
    if (!bh)
        return -EIO;

    dir_record = (struct ransomfs_dir_record*) bh->b_data;
    //iterate over records of single block
    while (off < RANSOMFS_BLOCK_SIZE) {
        
        //async add file to folder
        if (dir_record->ino == 0)
            if (__sync_bool_compare_and_swap(&dir_record->ino, 0, ino)) {
                dir_record->name_len = strlen(filename);
                memcpy(dir_record->filename, filename, dir_record->name_len+1);
                dir_record->file_type = file_type;
                mark_buffer_dirty(bh);
                brelse(bh);
                //success
                return 1;
            }
        dir_record++;
        off += sizeof(struct ransomfs_dir_record);
    }

    brelse(bh);
    //failure
    return 0;
}

//restrieves the next last logical block allocated from an extent tree
uint32_t get_last_logical_block_no(struct super_block* sb, struct ransomfs_extent_header *block_head) {
    
    struct buffer_head* bh;
    struct ransomfs_extent_idx* curr_node;
    struct ransomfs_extent* last_leaf;

    while (block_head->depth != 0) {

        //last node
        curr_node = (struct ransomfs_extent_idx*) &block_head[block_head->entries];

        bh = sb_bread(sb, curr_node->leaf_block);
        block_head = (struct ransomfs_extent_header*) bh->b_data;

        //check magic
        if (block_head->magic != RANSOMFS_EXTENT_MAGIC) {
            printk(KERN_ERR "FATAL ERROR: Corrupted exent tree\n");
            brelse(bh);
            return -ENOTRECOVERABLE;
        }

        brelse(bh);
    }

    last_leaf = (struct ransomfs_extent*) &block_head[block_head->entries];

    return last_leaf->file_block + last_leaf->len - 1;

}

// recursive function to read and extent tree of a directory find some free space and  add a filename
// TODO every fucntion that reads the exent tree is the same, is there a better way to do this?
// return 0 if successful
int add_file_to_directory(struct super_block* sb, struct ransomfs_extent_header *block_head, const unsigned char* filename, uint32_t ino, uint8_t file_type) {

    int i = 0, j = 0, ret;
    struct buffer_head *bh = NULL;
    uint32_t new_block_no, last_logic_no;

    printk(KERN_INFO "Started the read of the extent tree for add file\n");

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        struct ransomfs_extent* curr_leaf;

        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {
            curr_leaf = (struct ransomfs_extent*) &block_head[i+1];
            
            printk(KERN_INFO "Reading entry %d\n", i);
            printk(KERN_INFO"%ld %ld %ld\n", sizeof(struct ransomfs_extent), sizeof(struct ransomfs_extent_header), sizeof(struct ransomfs_extent_idx));
            printk(KERN_INFO "Start data block: %x - len: %d\n", curr_leaf->data_block, curr_leaf->len);
                
            //iterate over data block of single extent
            for (j = curr_leaf->data_block; j < curr_leaf->data_block + curr_leaf->len; j++) {     
                
                ret = add_file_to_dir_record(sb, j, filename, ino, file_type);
                if (ret > 0) {
                    printk(KERN_INFO "Added %s in %x\n", filename, j);
                    return 0;
                }
           }
        }

    } else {

        //tree has depth zero call this function again at lower levels
        struct ransomfs_extent_idx* curr_node;
        struct ransomfs_extent_header* new_block_head;
        int ret;
        
        for (i = 0; i < block_head->entries; i++) {

            printk("Entering entry %d", i);
            curr_node = (struct ransomfs_extent_idx*) &block_head[i+1];

            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct ransomfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != RANSOMFS_EXTENT_MAGIC) {
                printk(KERN_ERR "FATAL ERROR: Corrupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            //call again woth new block head
            ret = add_file_to_directory(sb, new_block_head, filename, ino, file_type);

            if (ret == 0) { 
                //operation was successful, we can stop here   
                brelse(bh);
                return ret;
            }

            //operation failed try again with next node
            brelse(bh);
        }

    }

    printk(KERN_INFO "Extent full for directory - expanding\n");
    //failed - we no more space in this extent - allocate a new block and add the file there
    //find the new logical block number
    last_logic_no = get_last_logical_block_no(sb, block_head);
    printk(KERN_INFO "Last logical block is %d\n", last_logic_no);
    if (last_logic_no < 0) {
        return -ENOTRECOVERABLE;
    }
    new_block_no = ransomfs_allocate_new_block(sb, block_head, last_logic_no+1, 0);

    printk(KERN_INFO "new physical block is %d\n", new_block_no);
    ret = add_file_to_dir_record(sb, new_block_no, filename, ino, file_type);

    printk(KERN_INFO "Extent tree updated\n");
    //TODO ugly?
    if (ret < 0)
        return ret;
    else if (ret > 0)
        return 0;
    else
        return -1;
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
            printk(KERN_INFO "Start data block: %x - len: %u\n", curr_leaf->data_block, curr_leaf->len);

            //iterate over data block of single extent
            for (j = curr_leaf->data_block; j < curr_leaf->data_block + curr_leaf->len; j++) {     
                
                bh = sb_bread(sb, j);
                if (!bh)
                    return -EIO;

                off = 0;
                curr_record = (struct ransomfs_dir_record*) bh->b_data;
                //iterate over records of single block
                while (off < RANSOMFS_BLOCK_SIZE) {
                    if (curr_record->ino != 0) {
                        printk(KERN_INFO "found %s\n", curr_record->filename);
                        if (file_counter >= ctx->pos-2) {
                            if (!dir_emit(ctx, curr_record->filename, RANSOMFS_MAX_FILENAME, curr_record->ino, curr_record->file_type)) {
                                brelse(bh);
                                return file_counter; //no more space
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
        struct ransomfs_extent_header* new_block_head;
        for (i = 0; i < block_head->entries; i++) {

            printk("Entering entry %d", i);
            curr_node = (struct ransomfs_extent_idx*) &block_head[i+1];

            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct ransomfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != RANSOMFS_EXTENT_MAGIC) {
                printk(KERN_ERR "FATAL ERROR: Corrupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            //call again with new block head
            file_counter += read_dir_extent_tree(sb, ctx, new_block_head);

            brelse(bh);
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
    if (ctx->pos < 2) {
        if (!dir_emit_dots(dir, ctx)) //FIXME: not working for root, why?
	        return 0;
    }
    //iterate over data blocks with extents
    return read_dir_extent_tree(sb, ctx, ci->extent_tree);
}

const struct file_operations ransomfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = ransomfs_iterate,
};