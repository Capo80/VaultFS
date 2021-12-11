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
    
    printk(KERN_INFO "Started the search for logical block: %d\n", logical_block_no);

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

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


//allocares a new block into the exent trying trying to find space that is close to the last leaf group idx
uint32_t ransomfs_allocate_new_block(struct super_block* sb, struct ransomfs_extent_header* block_head, uint32_t logical_block_no) {

    uint16_t i = 0, c;
    uint32_t phys_block_no, group_idx, last_group_idx, last_phys_block_no, distance, nr_new_blocks;
    uint32_t curr_alloc = 0;
    struct buffer_head* bh;
    struct ransomfs_extent* new_leaf, *last_leaf;
    unsigned long* data_bitmap;

    printk(KERN_INFO "Allocating new logical block: %d\n", logical_block_no);

    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        if (block_head->entries == block_head->max) {
            //TODO allocate new level
        } else {

            last_leaf = (struct ransomfs_extent*) &block_head[block_head->entries];

            last_phys_block_no = last_leaf->data_block + last_leaf->len;
            group_idx = last_phys_block_no % RANSOMFS_BLOCKS_PER_GROUP;
            nr_new_blocks = last_leaf->file_block + last_leaf->len - logical_block_no;

            if (nr_new_blocks - logical_block_no < 0) {
                printk(KERN_INFO "The block is already allocated\n");
                return ransomfs_exent_search_block(block_head, logical_block_no);
            }

            //not sure how many blocks we should allocate - for now only 1 but it may be better to allocatare more at the time
            //TODO maybe do a function for this, there is similar code in the inode file

            //read gdt if necessary
            if (gdt == NULL) {
                bh = sb_bread(sb, RANSOMFS_GDT_BLOCK_NR);
                if (!bh)
                    return -EIO;

                gdt = kzalloc(RANSOMFS_BLOCK_SIZE, GFP_KERNEL);  //TODO cache maybe?
                memcpy(gdt, bh->b_data, RANSOMFS_BLOCK_SIZE);        
            }

            //find closest group with free space
            c = 0;
            while (c < RANSOMFS_GROUPDESC_PER_BLOCK) {  //can be done faster but gdt is very small so not needed i think
                if (gdt[c].free_blocks_count >= nr_new_blocks && last_group_idx - c < distance) {
                    distance = last_group_idx - c;
                    group_idx = c;                      
                }
                c++;
            }

            printk(KERN_INFO "Found space for new block at group %u\n", group_idx);
            
            //no space left
            if (distance == RANSOMFS_GROUPDESC_PER_BLOCK+1)
                return -ENOSPC;

            //update GDT in memory
            gdt[c].free_blocks_count -= nr_new_blocks;                //FIXME need concurrency checks here (just a __sync with a check on 0)

            //update GDT on disk
            bh = sb_bread(sb, RANSOMFS_GDT_BLOCK_NR);
            memcpy(bh->b_data, gdt, RANSOMFS_BLOCK_SIZE); //TODO we don't need to do this every time, just every now and then
            mark_buffer_dirty(bh);                        //     to prevent data loss in case of failures
            brelse(bh);

            mutex_lock_interruptible(&data_bitmap_mutex);

            //here we know there is enough space we just need to find it
            //look for contiguos blocks first - if we can't find it, look for smaller spaces

            //load data bitmap from disk
            bh = sb_bread(sb, 2 + group_idx*RANSOMFS_BLOCKS_PER_GROUP);
            data_bitmap = (unsigned long*) bh->b_data;

            printk("bitmap: %lx", *data_bitmap);

            while (nr_new_blocks > 0 && curr_alloc < nr_new_blocks) {    
                phys_block_no = bitmap_find_next_zero_area(data_bitmap, RANSOMFS_BLOCK_SIZE, 0, nr_new_blocks, 0);
                if (phys_block_no < RANSOMFS_BLOCK_SIZE) {

                    printk(KERN_INFO "Found %d free blocks at index %u\n", nr_new_blocks, phys_block_no);

                    //found space
                    curr_alloc += nr_new_blocks;

                    //update datablock bitmap
                    bitmap_set(data_bitmap, phys_block_no, nr_new_blocks);
                    mark_buffer_dirty(bh);
                    brelse(bh);
                    
                    //add new leaf to exent tree
                    if (block_head->entries == block_head->max) {
                        //TODO allocate new block for exent tree
                        gdt[c].free_blocks_count += nr_new_blocks;
                        return -ENOSPC;
                    }
                    last_leaf = (struct ransomfs_extent*) &block_head[block_head->entries];
                    new_leaf = (struct ransomfs_extent*) &block_head[block_head->entries+1];
                    new_leaf->file_block = last_leaf->file_block + last_leaf->len;
                    new_leaf->len = nr_new_blocks;
                    new_leaf->data_block = phys_block_no;

                    //update head
                    block_head->entries++;

                } else {
                    //cloud not find space - look for smaller intervals
                    nr_new_blocks--;
                    
                    printk(KERN_INFO "Clould not fined %d free blocks\n", nr_new_blocks);
                }

            }

            if (nr_new_blocks <= 0) {
                printk(KERN_ERR "FATAL ERROR: Corrupted GDT, found no data block avaible\n"); //should never happen
                gdt[c].free_blocks_count += nr_new_blocks;
                return -ENOSPC;
            }
            
            mutex_unlock(&data_bitmap_mutex);

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

void ransomfs_init_extent_tree(struct ransomfs_inode_info* inode, uint32_t first_block_no) {

    struct ransomfs_extent_header head;
    struct ransomfs_extent leaf;

    //tree head
    memset(&head, 0, sizeof(struct ransomfs_extent_header));
    head.magic = RANSOMFS_EXTENT_MAGIC;
    head.entries = 1;
    head.max = RANSOMFS_EXTENT_PER_INODE;
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