#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "ransomfs.h"

//find file phisical block by his logical block number 
uint32_t ransomfs_exent_search_block(struct ransomfs_extent_header* block_head, uint32_t logical_block_no) {

    uint16_t i = 0;
    struct ransomfs_extent* curr_leaf;
    
    AUDIT(TRACE)
    printk(KERN_INFO "Started the search for logical block: %d\n", logical_block_no);

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        AUDIT(TRACE)
        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {

            curr_leaf = (struct ransomfs_extent*) &block_head[i+1];
            
            if (curr_leaf->file_block <= logical_block_no && curr_leaf->file_block + curr_leaf->len > logical_block_no)    
                return curr_leaf->data_block + (logical_block_no - curr_leaf->file_block);
            
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

    // zero means not found - the function cannot return 0 because thats the superblock
    return 0;

}

block_pos_t ransomfs_allocate_tree_node(struct super_block* sb, unsigned short depth, uint32_t initial_logical_block, uint32_t initial_phys_block) {

    block_pos_t new_block_pos;
    struct buffer_head* bh;
    struct ransomfs_extent_header* curr_head;
    struct ransomfs_extent_idx* curr_idx;
    uint32_t phys_block_no;
    
    AUDIT(TRACE)
    printk(KERN_INFO "Allocating a new tree block with depth %d\n", depth);
    
    //we need depth+1 blocks for the tree
    //TODO they don't need to be contiguos
    new_block_pos = ransomfs_get_free_blocks(sb, initial_phys_block, depth+1);
    if (new_block_pos.error == 1)
        return new_block_pos;

    //write all depth > 0 nodes
    phys_block_no = RANSOMFS_POS_TO_PHYS(new_block_pos.group_idx, new_block_pos.block_idx);
    while (depth > 0) {
        bh = sb_bread(sb, phys_block_no+depth);
        curr_head = (struct ransomfs_extent_header*) bh->b_data;
        curr_head->magic = RANSOMFS_EXTENT_MAGIC;
        curr_head->depth = cpu_to_le16(depth);
        curr_head->max = cpu_to_le16(RANSOMFS_EXTENT_PER_BLOCK);
        curr_head->entries = 1;

        curr_idx = (struct ransomfs_extent_idx*) curr_head + 1;
        curr_idx->file_block = cpu_to_le32(initial_logical_block);
        curr_idx->leaf_block = cpu_to_le32(phys_block_no+depth-1);
        mark_buffer_dirty(bh);
        brelse(bh);

        depth--;
    }
    
    //write depth = 0 node - depth 0 does not have an idx
    bh = sb_bread(sb, phys_block_no+depth);
    curr_head = (struct ransomfs_extent_header*) bh->b_data;
    curr_head->magic = RANSOMFS_EXTENT_MAGIC;
    curr_head->depth = 0;
    curr_head->max = cpu_to_le16(RANSOMFS_EXTENT_PER_BLOCK);
    curr_head->entries = 0;
    mark_buffer_dirty(bh);
    brelse(bh);
    
    return new_block_pos;
}

//allocates a new block into the exent trying trying to find space that is close to the last leaf group idx
uint32_t ransomfs_allocate_new_block_rec(struct super_block* sb, struct ransomfs_extent_header* block_head, uint32_t logical_block_no, uint32_t initial_logical_block, uint32_t initial_phys_block) {

    uint16_t i = 0, c;
    uint32_t phys_block_no = 0, group_idx, last_phys_block_no, nr_new_blocks, temp;
    uint32_t curr_alloc = 0;
    struct buffer_head* bh;
    struct ransomfs_sb_info* sbi = RANSOMFS_SB(sb);
    struct ransomfs_extent* new_leaf, *last_leaf;
    struct ransomfs_extent_header* new_block_head;
    block_pos_t new_block_pos;
    unsigned long* data_bitmap;

    AUDIT(TRACE)
    printk(KERN_INFO "Allocating new logical block: %d\n", logical_block_no);

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        AUDIT(TRACE)
        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        if (block_head->entries == block_head->max) {
            //no more space
            return 0;
        } else {
            
            if (block_head->entries != 0) {
                last_leaf = (struct ransomfs_extent*) &block_head[block_head->entries];

                last_phys_block_no = last_leaf->data_block + last_leaf->len;
                group_idx = last_phys_block_no % RANSOMFS_BLOCKS_PER_GROUP;
                nr_new_blocks = logical_block_no - (last_leaf->file_block + last_leaf->len) + 1;
            } else {
                nr_new_blocks = logical_block_no - initial_logical_block + 1;
            }
            if (nr_new_blocks < 0) {
                AUDIT(ERROR)
                printk(KERN_INFO "The block is already allocated\n");
                return ransomfs_exent_search_block(block_head, logical_block_no);
            }

            AUDIT(DEBUG)
            printk("Allocating %d blocks\n", nr_new_blocks);

            //not sure how many blocks we should allocate - for now only 1 but it may be better to allocatare more at the time
            temp = nr_new_blocks;
            while (nr_new_blocks > 0 && curr_alloc < temp) { 
                AUDIT(DEBUG)
                printk(KERN_INFO "Looking for %d blocks\n", nr_new_blocks);   
                new_block_pos = ransomfs_get_free_blocks(sb, initial_phys_block, nr_new_blocks);
                if (new_block_pos.error == 0) {
                    
                    //get real phys block number
                    phys_block_no = RANSOMFS_POS_TO_PHYS(new_block_pos.group_idx, new_block_pos.block_idx);
                    
                    AUDIT(DEBUG)
                    printk(KERN_INFO "Found %d free blocks at index %u\n", nr_new_blocks, phys_block_no);

                    //found space
                    curr_alloc += nr_new_blocks;

                    //add new leaf to exent tree
                    if (block_head->entries == block_head->max) {
                        //no more space
                        //deallocate block
                        sbi->gdt[c].free_blocks_count += nr_new_blocks;
                        bh = sb_bread(sb, 2 + new_block_pos.group_idx*RANSOMFS_BLOCKS_PER_GROUP);
                        data_bitmap = (unsigned long*) bh->b_data;
                        mutex_lock_interruptible(&sbi->data_bitmap_mutex);
                        bitmap_clear(data_bitmap, new_block_pos.block_idx, nr_new_blocks);
                        mark_buffer_dirty(bh);
                        brelse(bh);
                        mutex_unlock(&sbi->data_bitmap_mutex);

                        return 0;
                    }

                    last_leaf = (struct ransomfs_extent*) &block_head[block_head->entries];
                    if (block_head->entries != 0 && last_leaf->data_block + last_leaf->len == phys_block_no) {
                        last_leaf->len += nr_new_blocks;
                    } else {
                        new_leaf = (struct ransomfs_extent*) &block_head[block_head->entries+1];
                        new_leaf->file_block = cpu_to_le32(last_leaf->file_block + last_leaf->len);
                        new_leaf->len = cpu_to_le16(nr_new_blocks);
                        new_leaf->data_block = cpu_to_le32(phys_block_no);

                        //save actual block numb to return
                        phys_block_no = phys_block_no + nr_new_blocks - 1;

                        //update head
                        block_head->entries++;
                    }

                } else {
                    //cloud not find space - look for smaller intervals
                    nr_new_blocks--;
                    
                    AUDIT(DEBUG)
                    printk(KERN_INFO "Clould not find %d free blocks\n", nr_new_blocks);
                }

            }


            if (nr_new_blocks <= 0) {
                //we have no space left
                return -ENOSPC;
            }

            return phys_block_no;
        }
    } else {
        //tree has depth > zero call this function again at lower levels
        struct ransomfs_extent_idx* curr_node;
        uint32_t ret = 0;
        for (i = 0; i < block_head->entries; i++) {
            
            AUDIT(TRACE)
            printk(KERN_INFO"Entering entry %d/%d", i, block_head->entries);
            curr_node = (struct ransomfs_extent_idx*) block_head + i + 1;

            AUDIT(TRACE)
            printk(KERN_INFO"Reading block %d", curr_node->leaf_block);
            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct ransomfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != RANSOMFS_EXTENT_MAGIC) {
                AUDIT(ERROR)
                printk(KERN_ERR "FATAL ERROR: Currupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            //call again woth new block head
            ret = ransomfs_allocate_new_block_rec(sb, new_block_head, logical_block_no, curr_node->file_block, curr_node->leaf_block);

            if (ret > 0) { 
                //operation was successful, we can stop here   
                mark_buffer_dirty(bh);
                brelse(bh);
                return ret;
            }

            //operation failed try again with next node
            brelse(bh);
        }

        //we failed at each node - try to allocate a new block at this level
        if (block_head->entries == block_head->max)
            return 0; //we cant
        else {

            new_block_pos = ransomfs_allocate_tree_node(sb, block_head->depth-1, initial_logical_block, initial_phys_block);
            if (new_block_pos.error == 1)
                return 0; //failed to allocate

            AUDIT(TRACE)
            printk(KERN_INFO "New tree allocated\n");

            //get real phys block number
            phys_block_no = group_idx*RANSOMFS_BLOCKS_PER_GROUP + RANSOMFS_INODES_GROUP_BLOCK_COUNT + 4 + new_block_pos.block_idx;
            
            //update entry
            curr_node = (struct ransomfs_extent_idx*) &block_head[block_head->entries+1];
            last_leaf = (struct ransomfs_extent*) &block_head[block_head->entries];
            curr_node->file_block = cpu_to_le32(last_leaf->file_block + last_leaf->len);
            curr_node->leaf_block = cpu_to_le32(phys_block_no);

            //update header
            block_head->entries++;

            //new tree allocated rerun this funcion on the block head again - this time we will have space
            return ransomfs_allocate_new_block_rec(sb, block_head, logical_block_no, initial_logical_block, initial_phys_block);

        }
        return ret;
    }

}

uint32_t ransomfs_allocate_new_block(struct super_block* sb, struct ransomfs_extent_header* block_head, uint32_t logical_block_no, uint32_t initial_phys_block) {

    block_pos_t new_block_pos;
    uint32_t phys_block_no;
    uint16_t original_depth = block_head->depth;
    uint32_t original_file_block;
    struct ransomfs_extent_idx* curr_idx;
    struct ransomfs_extent_header* old_head;
    uint16_t original_max = block_head->max;
    struct buffer_head* bh;

    //check if we have space in this head (and if it not the max depth)
    if (block_head->entries == block_head->max && block_head->depth < RANSOMFS_MAX_DEPTH) {
        
        //if we don't we move the current head to a new block and we init another head of depth + 1
        new_block_pos = ransomfs_get_free_blocks(sb, initial_phys_block, 1);
        if (new_block_pos.error == 1)
            return new_block_pos.group_idx; //we failed return error

        //move
        phys_block_no = RANSOMFS_POS_TO_PHYS(new_block_pos.group_idx, new_block_pos.block_idx);
        
        AUDIT(DEBUG)
        printk(KERN_INFO"Moving tree to %d\n", phys_block_no);
        
        bh = sb_bread(sb, phys_block_no);

        memcpy(bh->b_data, block_head, sizeof(struct ransomfs_extent_header)*(block_head->max+1));
        old_head = (struct ransomfs_extent_header*) bh->b_data;
        old_head->max = RANSOMFS_EXTENT_PER_BLOCK;

        mark_buffer_dirty(bh);
        brelse(bh);

        //update
        curr_idx = (struct ransomfs_extent_idx*) block_head + 1;
        original_file_block = curr_idx->file_block;
        memset(block_head, 0, sizeof(struct ransomfs_extent_header)*(block_head->max+1));
        block_head->magic = RANSOMFS_EXTENT_MAGIC;
        block_head->depth = original_depth+1;
        block_head->max = original_max;
        block_head->entries = 1;

        curr_idx->file_block = original_file_block;
        curr_idx->leaf_block = phys_block_no;
    }

    return ransomfs_allocate_new_block_rec(sb, block_head, logical_block_no, 0, initial_phys_block);
    
}
void ransomfs_init_extent_tree(struct ransomfs_inode_info* inode, uint32_t first_block_no) {

    struct ransomfs_extent_header head;
    struct ransomfs_extent leaf;

    //tree head
    memset(&head, 0, sizeof(struct ransomfs_extent_header));
    head.magic = RANSOMFS_EXTENT_MAGIC;
    head.entries = 1;
    head.max = RANSOMFS_EXTENT_PER_INODE-1;
    head.depth = 0;
       
    //the only leaf
    memset(&leaf, 0, sizeof(struct ransomfs_extent_header));
    leaf.file_block = 0;
    leaf.len = 1;
    leaf.data_block = first_block_no;

    //save to inode
    memcpy(inode->extent_tree, &head, sizeof(struct ransomfs_extent_header));
    memcpy(inode->extent_tree + 1, &leaf, sizeof(struct ransomfs_extent_header));

}