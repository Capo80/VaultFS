#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitmap.h>

#include "ransomfs.h"

struct ransomfs_group_desc* gdt = NULL;
DEFINE_MUTEX(gdt_mutex);
DEFINE_MUTEX(inode_bitmap_mutex);
DEFINE_MUTEX(data_bitmap_mutex);

//recursive function that reads the extent tree and finds an inode number
// TODO pretty much the same as the function that updates the context, is there a better way to do this?
// returns the inode number if found,  anegative number if not
static int find_inode_by_name(struct super_block* sb, struct ransomfs_extent_header *block_head, const char* to_search) {

    int i = 0, j = 0, off = 0;
    struct buffer_head *bh = NULL;

    printk(KERN_INFO "Started the search for an inode\n");

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
                        if (strcmp(to_search, curr_record->filename) == 0)
                            return curr_record->ino;
                    
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

    return -1;
}


/* Get inode ino from disk */
struct inode *ransomfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode = NULL;
    struct ransomfs_inode *cinode = NULL;
    struct ransomfs_inode_info *ci = NULL;
    struct ransomfs_sb_info *sbi = RANSOMFS_SB(sb);
    struct buffer_head *bh = NULL;
    uint32_t inode_bg = ino / RANSOMFS_INODES_PER_GROUP;
    uint32_t inode_shift = ino % RANSOMFS_INODES_PER_GROUP;
    uint32_t inode_block = 4 + inode_bg * RANSOMFS_BLOCKS_PER_GROUP + inode_shift / RANSOMFS_INODES_PER_BLOCK;
    int ret;

    printk(KERN_INFO "Inode block is at (%d, %d)", inode_block, inode_shift);

    // TODO check bitmap somewhere here? maybe?
    
    /* Fail if ino is out of range */
    if (ino >= sbi->inodes_count)
        return ERR_PTR(-EINVAL);

    /* Get a locked inode from Linux */
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    /* If inode is in cache, return it */
    if (!(inode->i_state & I_NEW))
        return inode;

    ci = RANSOMFS_INODE(inode); //not sure about this for now
    /* Read inode from disk and initialize */
    bh = sb_bread(sb, inode_block);
    if (!bh) {
        ret = -EIO;
        brelse(bh);
    	iget_failed(inode);
    	return ERR_PTR(ret);
    }
    cinode = (struct ransomfs_inode *) bh->b_data;
    cinode += inode_shift;

    inode->i_ino = ino;
    inode->i_sb = sb;
    //inode->i_op = &ransomfs_inode_ops;

    inode->i_mode = le16_to_cpu(cinode->i_mode);
    i_uid_write(inode, le16_to_cpu(cinode->i_uid));
    i_gid_write(inode, le16_to_cpu(cinode->i_gid));
    inode->i_size = le32_to_cpu(cinode->i_size);
    inode->i_ctime.tv_sec = (time64_t) le32_to_cpu(cinode->i_ctime);
    inode->i_ctime.tv_nsec = 0;
    inode->i_atime.tv_sec = (time64_t) le32_to_cpu(cinode->i_atime);
    inode->i_atime.tv_nsec = 0;
    inode->i_mtime.tv_sec = (time64_t) le32_to_cpu(cinode->i_mtime);
    inode->i_mtime.tv_nsec = 0;
    inode->i_blocks = le32_to_cpu(cinode->i_blocks);

    //copy extent tree
    memcpy(ci->extent_tree, cinode->extent_tree, sizeof(struct ransomfs_extent_header)*cinode->extent_tree[0].entries);

    if (S_ISDIR(inode->i_mode)) {
        //ci->dir_block = le32_to_cpu(cinode->dir_block);
        printk("This file is a directory\n");
        inode->i_op = &ransomfs_inode_ops;
        inode->i_fop = &ransomfs_dir_ops;
    } else if (S_ISREG(inode->i_mode)) {
        //ci->ei_block = le32_to_cpu(cinode->ei_block);
        //inode->i_fop = &ransomfs_file_ops;
        //inode->i_mapping->a_ops = &ransomfs_aops;
    } else if (S_ISLNK(inode->i_mode)) {
        //strncpy(ci->i_data, cinode->i_data, sizeof(ci->i_data));
        //inode->i_link = ci->i_data;
        //inode->i_op = &symlink_inode_ops;
    }

    brelse(bh);

    /* Unlock the inode to make it usable */
    unlock_new_inode(inode);

    return inode;

}

// look for inode in directory with dentry
// return NULL on success, the inode will be connected to the dentry
struct dentry *ransomfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{
    struct super_block* sb = parent_inode->i_sb;
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(parent_inode);
    struct inode* inode = NULL;
    int ino;

    printk(KERN_INFO "look up called\n");

    //check filename size
    if (child_dentry->d_name.len > RANSOMFS_MAX_FILENAME)
        return ERR_PTR(-ENAMETOOLONG);

    //search for inode in directory
    ino = find_inode_by_name(sb, ci->extent_tree, child_dentry->d_name.name);
    if (ino >= 0) {
        printk(KERN_INFO "inode for %s found, it is number %d\n", child_dentry->d_name.name, ino);

        //read the new inode from disk and connect it to the dentry
        inode = ransomfs_iget(sb, ino);
        parent_inode->i_atime = current_time(parent_inode); //update access time
        mark_inode_dirty(parent_inode);
        d_add(child_dentry, inode);

    } else {
        printk(KERN_INFO "inode for %s not found\n", child_dentry->d_name.name);
        return ERR_PTR(ino);
    }
    
	return NULL;
}

static int ransomfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct super_block* sb;
    struct ransomfs_group_desc* temp;
    unsigned long* data_bitmap, *inode_bitmap;
    struct buffer_head* bh;
    unsigned short curr_space;
    uint32_t group_idx, distance = RANSOMFS_GROUPDESC_PER_BLOCK+1, c;
    uint32_t parent_group_idx = dir->i_ino / RANSOMFS_INODES_PER_GROUP;
    uint32_t data_block_idx, inode_table_idx;

    // Check filename length
    if (strlen(dentry->d_name.name) > RANSOMFS_MAX_FILENAME)
        return -ENAMETOOLONG;

    //check mode
    if (!S_ISDIR(mode) && !S_ISREG(mode)) {
        printk(KERN_ERR "File type not supported");
        return -EINVAL;
    }

    //read gdt if necessary
    if (gdt == NULL) {
        sb = dir->i_sb;
        bh = sb_bread(sb, RANSOMFS_GDT_BLOCK_NR);
        if (!bh)
            return -EIO;

        gdt = kzalloc(RANSOMFS_BLOCK_SIZE, GFP_KERNEL);  //TODO cache maybe?
        memcpy(gdt, bh->b_data, RANSOMFS_BLOCK_SIZE);

        
    }

    //find closest group with free space
    c = 0;
    while (c < RANSOMFS_GROUPDESC_PER_BLOCK) {  //can be done faster but gdt is very small so not needed i think
        if (gdt[c].free_blocks_count != 0 && parent_group_idx - c < distance) {
            distance = parent_group_idx - c;
            group_idx = c;                      
        }
        temp++;
    }

    printk(KERN_INFO "Found space for new file at group %u\n", group_idx);
    
    //no space left
    if (distance == RANSOMFS_GROUPDESC_PER_BLOCK+1)
        return -ENOSPC;

    //update GDT in memory
    gdt[c].free_blocks_count--;                //FIXME need concurrency checks here
    gdt[c].free_inodes_count--;

    //update GDT on disk
    memcpy(bh->b_data, gdt, RANSOMFS_BLOCK_SIZE); //TODO we don't need to do this every time, just every now and then
    mark_buffer_dirty(bh);                        //     to prevent data loss in case of failures
    brelse(bh);

    /*
        Find free data blocks within group
        - Look for RANSOMFS_INITIAL_FILE_SPACE contiguos blocks
        - if we fail lower the number of block until we reach 1
        - we will always find at least one block beacuse the GDT has already been updated
    */
    //load data bitmap from disk
    bh = sb_bread(sb, 2 + group_idx*RANSOMFS_BLOCKS_PER_GROUP);
    data_bitmap = (unsigned long*) bh->b_data;
    mutex_lock_interruptible(&data_bitmap_mutex);

    curr_space = RANSOMFS_INITIAL_FILE_SPACE;
    while ( curr_space > 0  && (data_block_idx = bitmap_find_next_zero_area(data_bitmap, RANSOMFS_BLOCK_SIZE, 0, curr_space, 0)) > RANSOMFS_BLOCK_SIZE)
        curr_space--;

    printk(KERN_INFO "Found %d free blocks at index %u\n", curr_space, data_block_idx);

    if (curr_space == 0) {
        printk(KERN_ERR "FATAL ERROR: Corrupted GDT, found no data block avaible\n"); //should never happen
        //FIXME we need to roll back the GDT if we fail here (even though its already corrupted)
        return -ENOSPC;
    }

    //update bitmap
    bitmap_fill(data_bitmap + data_block_idx, 1);
    mark_buffer_dirty(bh);
    brelse(bh);
    
    mutex_unlock(&data_bitmap_mutex);

    //find free space for inode in table
    //we have a lot more space than needed for inodes, so if we found a data block we will always find space for an inode
    bh = sb_bread(sb, 2 + group_idx*RANSOMFS_BLOCKS_PER_GROUP + 1);
    inode_bitmap = (unsigned long*) bh->b_data;
    mutex_lock_interruptible(&inode_bitmap_mutex);

    inode_table_idx = bitmap_find_next_zero_area(inode_bitmap, RANSOMFS_BLOCK_SIZE, 0, 1, 0);

    //update bitmap
    bitmap_fill(data_bitmap + inode_table_idx, 1);
    mark_buffer_dirty(bh);
    brelse(bh);

    mutex_unlock(&inode_bitmap_mutex);

    //create the new inode
    // TODO
}

static const struct inode_operations ransomfs_inode_ops = {
    .lookup = ransomfs_lookup,
};