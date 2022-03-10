#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "vaultfs.h"

void vaultfs_init_extent_tree(struct vaultfs_inode_info* inode, uint32_t first_block_no, uint32_t first_node_len) {

    struct vaultfs_extent_header head;
    struct vaultfs_extent leaf;

    //tree head
    memset(&head, 0, sizeof(struct vaultfs_extent_header));
    head.magic = VAULTFS_EXTENT_MAGIC;
    head.entries = 1;
    head.max = VAULTFS_EXTENT_PER_INODE-1;
    head.depth = 0;
       
    //the only leaf
    memset(&leaf, 0, sizeof(struct vaultfs_extent_header));
    leaf.file_block = 0;
    leaf.len = first_node_len;
    leaf.data_block = first_block_no;

    //save to inode
    memcpy(inode->extent_tree, &head, sizeof(struct vaultfs_extent_header));
    memcpy(inode->extent_tree + 1, &leaf, sizeof(struct vaultfs_extent_header));

}

//find file phisical block by his logical block number 
//return block_number in success - 0 in failure
uint32_t vaultfs_extent_search_block(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t logical_block_no) {

    uint16_t i = 0;
    struct vaultfs_extent* curr_leaf;
    
    AUDIT(TRACE)
    printk(KERN_INFO "Started the search for logical block: %d\n", logical_block_no);

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        AUDIT(TRACE)
        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {

            curr_leaf = (struct vaultfs_extent*) &block_head[i+1];
            
            if (curr_leaf->file_block <= logical_block_no && curr_leaf->file_block + curr_leaf->len > logical_block_no)    
                return curr_leaf->data_block + (logical_block_no - curr_leaf->file_block);
            
        }

    } else {
        //tree has depth zero call this function again at lower levels
        struct vaultfs_extent_idx* curr_node;
        struct vaultfs_extent_header* new_block_head;
        int ret;
        struct buffer_head* bh;

        for (i = 0; i < block_head->entries; i++) {
            
            AUDIT(TRACE)
            printk("Entering entry %d", i);
            curr_node = (struct vaultfs_extent_idx*) &block_head[i+1];

            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct vaultfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != VAULTFS_EXTENT_MAGIC) {
                AUDIT(ERROR)
                printk(KERN_ERR "FATAL ERROR: Corrupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            //call again with new block head
            ret = vaultfs_extent_search_block(sb, new_block_head, logical_block_no);

            if (ret != 0) { 
                //operation was successful, we can stop here   
                brelse(bh);
                return ret;
            }

            //operation failed try again with next node
            brelse(bh);
        }

    }

    // zero means not found - the function cannot return 0 because thats the superblock
    return 0;

}

//restrieves the next last logical block allocated from an extent tree
//also retriesves the last phys if the pointer is not NULL
uint32_t get_last_logical_block_no(struct super_block* sb, struct vaultfs_extent_header *block_head, uint32_t* last_phys_block) {
    
    struct buffer_head* bh;
    struct vaultfs_extent_idx* curr_node;
    struct vaultfs_extent* last_leaf;

    while (block_head->depth != 0) {

        //last node
        curr_node = (struct vaultfs_extent_idx*) &block_head[block_head->entries];

        bh = sb_bread(sb, curr_node->leaf_block);
        block_head = (struct vaultfs_extent_header*) bh->b_data;

        //check magic
        if (block_head->magic != VAULTFS_EXTENT_MAGIC) {
            AUDIT(ERROR)
            printk(KERN_ERR "FATAL ERROR: Corrupted exent tree\n");
            brelse(bh);
            return -ENOTRECOVERABLE;
        }

        brelse(bh);
    }

    last_leaf = (struct vaultfs_extent*) &block_head[block_head->entries];

    if (last_phys_block != NULL)
        *last_phys_block = last_leaf->data_block + last_leaf->len - 1;
    return last_leaf->file_block + last_leaf->len - 1;

}

//allocates a new tree node of the required depth that starts a initial logical block trying to allocate blocks near initial physical block
block_pos_t vaultfs_allocate_tree_node(struct super_block* sb, unsigned short depth, uint32_t initial_logical_block, uint32_t close_group_idx) {

    block_pos_t new_block_pos;
    struct buffer_head* bh;
    struct vaultfs_extent_header* curr_head;
    struct vaultfs_extent_idx* curr_idx;
    uint32_t phys_block_no;
    
    AUDIT(TRACE)
    printk(KERN_INFO "Allocating a new tree block with depth %d\n", depth);
    
    //we need depth+1 blocks for the tree
    //TODO they don't need to be contiguous
    new_block_pos = vaultfs_get_free_blocks(sb, close_group_idx, depth+1);
    if (new_block_pos.error == 1)
        return new_block_pos;

    //write all depth > 0 nodes
    phys_block_no = VAULTFS_POS_TO_PHYS(new_block_pos.group_idx, new_block_pos.block_idx);
    while (depth > 0) {
        bh = sb_bread(sb, phys_block_no+depth);
        curr_head = (struct vaultfs_extent_header*) bh->b_data;
        curr_head->magic = VAULTFS_EXTENT_MAGIC;
        curr_head->depth = cpu_to_le16(depth);
        curr_head->max = cpu_to_le16(VAULTFS_EXTENT_PER_BLOCK);
        curr_head->entries = 1;

        curr_idx = (struct vaultfs_extent_idx*) curr_head + 1;
        curr_idx->file_block = cpu_to_le32(initial_logical_block);
        curr_idx->leaf_block = cpu_to_le32(phys_block_no+depth-1);
        mark_buffer_dirty(bh);
        brelse(bh);

        depth--;
    }
    
    //write depth = 0 node - depth 0 does not have an idx
    bh = sb_bread(sb, phys_block_no+depth);
    curr_head = (struct vaultfs_extent_header*) bh->b_data;
    curr_head->magic = VAULTFS_EXTENT_MAGIC;
    curr_head->depth = 0;
    curr_head->max = cpu_to_le16(VAULTFS_EXTENT_PER_BLOCK);
    curr_head->entries = 0;
    mark_buffer_dirty(bh);
    brelse(bh);
    
    return new_block_pos;
}

//allocates a new block into the exent trying trying to find space that is close to the last leaf group idx
uint32_t vaultfs_allocate_new_block_rec(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t logical_block_no, uint32_t initial_logical_block, uint32_t close_group_idx) {

    uint16_t i = 0, c;
    uint32_t phys_block_no = 0, group_idx, last_phys_block_no, nr_new_blocks, temp;
    uint32_t curr_alloc = 0;
    struct buffer_head* bh;
    struct vaultfs_sb_info* sbi = VAULTFS_SB(sb);
    struct vaultfs_extent* new_leaf, *last_leaf;
    struct vaultfs_extent_header* new_block_head;
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
                last_leaf = (struct vaultfs_extent*) &block_head[block_head->entries];

                last_phys_block_no = last_leaf->data_block + last_leaf->len;
                group_idx = VAULTFS_GROUP_IDX(last_phys_block_no);
                nr_new_blocks = logical_block_no - (last_leaf->file_block + last_leaf->len) + 1;
            } else {
                nr_new_blocks = logical_block_no - initial_logical_block + 1;
            }
            if (nr_new_blocks < 0) {
                AUDIT(ERROR)
                printk(KERN_INFO "The block is already allocated\n");
                return vaultfs_extent_search_block(sb, block_head, logical_block_no);
            }

            AUDIT(DEBUG)
            printk("Allocating %d blocks\n", nr_new_blocks);

            //not sure how many blocks we should allocate - for now only 1 but it may be better to allocatare more at the time
            temp = nr_new_blocks;
            while (nr_new_blocks > 0 && curr_alloc < temp) { 
                AUDIT(DEBUG)
                printk(KERN_INFO "Looking for %d blocks\n", nr_new_blocks);   
                new_block_pos = vaultfs_get_free_blocks(sb, close_group_idx, nr_new_blocks);
                if (new_block_pos.error == 0) {
                    
                    //get real phys block number
                    phys_block_no = VAULTFS_POS_TO_PHYS(new_block_pos.group_idx, new_block_pos.block_idx);
                    
                    AUDIT(DEBUG)
                    printk(KERN_INFO "Found %d free blocks at index %u\n", nr_new_blocks, phys_block_no);

                    //found space
                    curr_alloc += nr_new_blocks;

                    //add new leaf to exent tree
                    if (block_head->entries == block_head->max) {

                        //no more space
                        //deallocate block
                        __sync_fetch_and_add(&sbi->gdt[c].free_blocks_count, nr_new_blocks);
                        __sync_fetch_and_add(&sbi->sb->free_blocks_count, nr_new_blocks);
                        bh = sb_bread(sb, VAULTFS_DATA_BITMAP_BLOCK_IDX(new_block_pos.group_idx));
                        data_bitmap = (unsigned long*) bh->b_data;
                        mutex_lock_interruptible(&sbi->data_bitmap_mutex);
                        bitmap_clear(data_bitmap, new_block_pos.block_idx, nr_new_blocks);
                        mark_buffer_dirty(bh);
                        brelse(bh);
                        mutex_unlock(&sbi->data_bitmap_mutex);

                        return 0;
                    }

                    last_leaf = (struct vaultfs_extent*) &block_head[block_head->entries];
                    if (block_head->entries != 0 && last_leaf->data_block + last_leaf->len == phys_block_no) {
                        last_leaf->len += nr_new_blocks;
                    } else {
                        new_leaf = (struct vaultfs_extent*) &block_head[block_head->entries+1];
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
        struct vaultfs_extent_idx* curr_node;
        uint32_t ret = 0;
        for (i = 0; i < block_head->entries; i++) {
            
            AUDIT(TRACE)
            printk(KERN_INFO"Entering entry %d/%d", i, block_head->entries);
            curr_node = (struct vaultfs_extent_idx*) block_head + i + 1;

            AUDIT(TRACE)
            printk(KERN_INFO"Reading block %d", curr_node->leaf_block);
            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct vaultfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != VAULTFS_EXTENT_MAGIC) {
                AUDIT(ERROR)
                printk(KERN_ERR "FATAL ERROR: Currupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            //call again woth new block head
            ret = vaultfs_allocate_new_block_rec(sb, new_block_head, logical_block_no, curr_node->file_block, VAULTFS_GROUP_IDX(curr_node->leaf_block));

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

            new_block_pos = vaultfs_allocate_tree_node(sb, block_head->depth-1, initial_logical_block, close_group_idx);
            if (new_block_pos.error == 1)
                return 0; //failed to allocate

            AUDIT(TRACE)
            printk(KERN_INFO "New tree allocated\n");

            //get real phys block number
            phys_block_no = VAULTFS_POS_TO_PHYS(group_idx, new_block_pos.block_idx);
            
            //update entry
            curr_node = (struct vaultfs_extent_idx*) &block_head[block_head->entries+1];
            last_leaf = (struct vaultfs_extent*) &block_head[block_head->entries];
            curr_node->file_block = cpu_to_le32(last_leaf->file_block + last_leaf->len);
            curr_node->leaf_block = cpu_to_le32(phys_block_no);

            //update header
            block_head->entries++;

            //new tree allocated rerun this function on the block head again - this time we will have space
            return vaultfs_allocate_new_block_rec(sb, block_head, logical_block_no, initial_logical_block, close_group_idx);

        }
        return ret;
    }

}

//start of the allocate block procedure, checks if the extend has enoght space, if it does it calls the recursive function
uint32_t vaultfs_allocate_new_block(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t logical_block_no, uint32_t close_group_idx) {

    block_pos_t new_block_pos;
    uint32_t phys_block_no;
    uint16_t original_depth = block_head->depth;
    uint32_t original_file_block;
    struct vaultfs_extent_idx* curr_idx;
    struct vaultfs_extent_header* old_head;
    uint16_t original_max = block_head->max;
    struct buffer_head* bh;

    //check if we have space in this head (and if it not the max depth)
    if (block_head->entries == block_head->max && block_head->depth < VAULTFS_MAX_DEPTH) {
        
        //if we don't we move the current head to a new block and we init another head of depth + 1
        new_block_pos = vaultfs_get_free_blocks(sb, close_group_idx, 1);
        if (new_block_pos.error == 1)
            return new_block_pos.group_idx; //we failed return error

        //move
        phys_block_no = VAULTFS_POS_TO_PHYS(new_block_pos.group_idx, new_block_pos.block_idx);
        
        AUDIT(DEBUG)
        printk(KERN_INFO"Moving tree to %d\n", phys_block_no);
        
        bh = sb_bread(sb, phys_block_no);

        memcpy(bh->b_data, block_head, sizeof(struct vaultfs_extent_header)*(block_head->max+1));
        old_head = (struct vaultfs_extent_header*) bh->b_data;
        old_head->max = VAULTFS_EXTENT_PER_BLOCK;

        mark_buffer_dirty(bh);
        brelse(bh);

        //update
        curr_idx = (struct vaultfs_extent_idx*) block_head + 1;
        original_file_block = curr_idx->file_block;
        memset(block_head, 0, sizeof(struct vaultfs_extent_header)*(block_head->max+1));
        block_head->magic = VAULTFS_EXTENT_MAGIC;
        block_head->depth = original_depth+1;
        block_head->max = original_max;
        block_head->entries = 1;

        curr_idx->file_block = original_file_block;
        curr_idx->leaf_block = phys_block_no;
    }

    return vaultfs_allocate_new_block_rec(sb, block_head, logical_block_no, 0, close_group_idx);
    
}

// frees the extent blocks from start to end of file by flipping bits in data bitmap
// start is a logical file block number (strart is deleted too)
// return 0 if success, < 0 if error
int vaultfs_free_extent_blocks(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t start) {

    int i = 0, distance, new_entry_number;
    struct buffer_head *bh = NULL;
    block_pos_t curr_pos;
    unsigned long* data_bitmap;
    struct vaultfs_sb_info* sbi = VAULTFS_SB(sb);

    AUDIT(TRACE)
    printk(KERN_INFO "Started the read of the extent tree for free blocks\n");

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        struct vaultfs_extent* curr_leaf;

        AUDIT(TRACE)
        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        new_entry_number = block_head->entries;
        for (i = 0; i < block_head->entries; i++) {
            curr_leaf = (struct vaultfs_extent*) &block_head[i+1];

            AUDIT(TRACE)    
            printk(KERN_INFO "Reading entry %d\n", i);
            AUDIT(TRACE)
            printk(KERN_INFO "Start file block: %x - len: %d, start: %d\n", curr_leaf->file_block, curr_leaf->len, start);
                
            if (curr_leaf->file_block + curr_leaf->len >= start) {

                // check if we need to delete the whole node or only a portion
                if (curr_leaf->file_block < start) {
                    distance = start - curr_leaf->file_block;
                } else {
                    distance = 0;
                    new_entry_number--; //need to remove this node
                }
                curr_pos = VAULTFS_PHYS_TO_POS(curr_leaf->data_block + distance);
                
                AUDIT(TRACE)
                printk(KERN_INFO "Freeing from group %d block %d len %d\n", curr_pos.group_idx, curr_pos.block_idx, curr_leaf->len - distance);

                //free blocks
                bh = sb_bread(sb, VAULTFS_DATA_BITMAP_BLOCK_IDX(curr_pos.group_idx));
                data_bitmap = (unsigned long*) bh->b_data;
                mutex_lock_interruptible(&sbi->data_bitmap_mutex);
                bitmap_clear(data_bitmap, curr_pos.block_idx, curr_leaf->len - distance);
                mark_buffer_dirty(bh);
                brelse(bh);
                mutex_unlock(&sbi->data_bitmap_mutex);

                //update len
                curr_leaf->len = distance;

            }
        }

        //update entry number
        block_head->entries = new_entry_number;

    } else {

        //tree has depth zero call this function again at lower levels
        struct vaultfs_extent_idx* curr_node;
        struct vaultfs_extent_header* new_block_head;
        int ret;
        struct buffer_head* bh2;
        
        for (i = 0; i < block_head->entries; i++) {
            
            AUDIT(TRACE)
            printk("Entering entry %d", i);
            curr_node = (struct vaultfs_extent_idx*) &block_head[i+1];

            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct vaultfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != VAULTFS_EXTENT_MAGIC) {
                AUDIT(ERROR)
                printk(KERN_ERR "FATAL ERROR: Corrupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            ret = vaultfs_free_extent_blocks(sb, new_block_head, start);
            if (ret < 0) { 
                //error - stop here - should be rare, errors here are rarely recoverable  
                brelse(bh);
                return ret;
            }

            //check if the block head still has entries
            if (new_block_head->entries == 0) {
                //free the block if it doesnt
                curr_pos = VAULTFS_PHYS_TO_POS(curr_node->leaf_block);

                bh2 = sb_bread(sb, VAULTFS_DATA_BITMAP_BLOCK_IDX(curr_pos.group_idx));
                data_bitmap = (unsigned long*) bh2->b_data;
                mutex_lock_interruptible(&sbi->data_bitmap_mutex);
                bitmap_clear(data_bitmap, curr_pos.block_idx, 1);
                mark_buffer_dirty(bh2);
                brelse(bh2);
                mutex_unlock(&sbi->data_bitmap_mutex);
            }

            //operation completed with this node - move to the next
            mark_buffer_dirty(bh);
            brelse(bh);
        }

    }

    return 0;

}