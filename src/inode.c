#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/version.h>

#include "ransomfs.h"

// recursive function that reads the extent tree and finds an inode number
// TODO every fucntion that reads the exent tree is the same, is there a better way to do this?
// returns the inode number if found,  a negative number if not
static int find_inode_by_name(struct super_block* sb, struct ransomfs_extent_header *block_head, const char* to_search) {

    int i = 0, j = 0, off = 0;
    struct buffer_head *bh = NULL;

    AUDIT(TRACE)
    printk(KERN_INFO "Started the search for an inode\n");
    
    //tree has depth zero, other records point to data blocks, read them directly
    if (block_head->depth == 0) {

        struct ransomfs_extent* curr_leaf;
        struct ransomfs_dir_record* curr_record;

        AUDIT(TRACE)
        printk(KERN_INFO "Tree has depth %d and %d entries\n", block_head->depth, block_head->entries);

        //iterate over extent leafs
        for (i = 0; i < block_head->entries; i++) {
            curr_leaf = (struct ransomfs_extent*) &block_head[i+1];
            
            AUDIT(TRACE)
            printk(KERN_INFO "Reading entry %d\n", i);

            AUDIT(TRACE)
            printk(KERN_INFO "Start data block: %d - len: %d\n", curr_leaf->data_block, curr_leaf->len);
                
            //iterate over data block of single extent
            for (j = curr_leaf->data_block; j < curr_leaf->data_block + curr_leaf->len; j++) {     
                
                bh = sb_bread(sb, j);
                if (!bh)
                    return -EIO;

                off = 0;
                curr_record = (struct ransomfs_dir_record*) bh->b_data;

                //iterate over records of single block
                while (off < RANSOMFS_BLOCK_SIZE) {
                    AUDIT(TRACE)
                    printk(KERN_INFO "Searching off %d ino %d", off, curr_record->ino);
                    if (curr_record->ino != 0)
                        if (strcmp(to_search, curr_record->filename) == 0) {
                            brelse(bh);
                            return curr_record->ino;
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
        unsigned int ret;
        
        for (i = 0; i < block_head->entries; i++) {

            AUDIT(TRACE)
            printk("Entering entry %d", i);
            curr_node = (struct ransomfs_extent_idx*) &block_head[i+1];

            bh = sb_bread(sb, curr_node->leaf_block);
            new_block_head = (struct ransomfs_extent_header*) bh->b_data;

            //check magic
            if (new_block_head->magic != RANSOMFS_EXTENT_MAGIC) {
                AUDIT(ERROR)
                printk(KERN_ERR "FATAL ERROR: Currupted exent tree\n");
                return -ENOTRECOVERABLE;
            }

            //call again woth new block head
            ret = find_inode_by_name(sb, new_block_head, to_search);

            if (ret > 0) { 
                //operation was successful, we can stop here   
                brelse(bh);
                return ret;
            }

            //operation failed try again with next node
            brelse(bh);
        }

        //failed return -1

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
    uint32_t inode_block = RANSOMFS_INODE_BLOCK_IDX(inode_bg, inode_shift);
    uint32_t inode_block_shift = inode_shift % RANSOMFS_INODES_PER_BLOCK;
    int ret;

    AUDIT(TRACE)
    printk(KERN_INFO "Inode block is at (%d, %d)", inode_block, inode_shift);
    
    /* Fail if ino is out of range */
    if (ino >= sbi->sb->inodes_count)
        return ERR_PTR(-EINVAL);

    /* Get a locked inode from Linux */
    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    /* If inode is in cache, return it */
    inode->i_sb = sb;
    if (!(inode->i_state & I_NEW)) {
        printk("inode in cache\n");
        return inode;
    }
    AUDIT(TRACE)
    printk(KERN_INFO "Reding inode from disk\n");

    /* Read inode from disk and initialize */
    bh = sb_bread(sb, inode_block);
    if (!bh) {
        ret = -EIO;
        brelse(bh);
    	iget_failed(inode);
    	return ERR_PTR(ret);
    }
    cinode = (struct ransomfs_inode *) bh->b_data;
    cinode += inode_block_shift;

    inode->i_ino = ino;
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

    AUDIT(TRACE)
    printk(KERN_INFO "Saved disk info on inode\n");

    //copy extent tree
    ci = RANSOMFS_INODE(inode);
    memcpy(ci->extent_tree, cinode->extent_tree, sizeof(struct ransomfs_extent_header)*RANSOMFS_EXTENT_PER_INODE);
    ci->i_committed = le16_to_cpu(cinode->i_committed);

    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &ransomfs_dir_inode_ops;
        inode->i_fop = &ransomfs_dir_ops;
    } else if (S_ISREG(inode->i_mode) || S_ISMS(inode->i_mode) || S_ISFW(inode->i_mode)) {
        inode->i_op = &ransomdfs_file_inode_ops;
        inode->i_fop = &ransomfs_file_ops;
        inode->i_mapping->a_ops = &ransomfs_aops;
    }

    sbi = RANSOMFS_SB(inode->i_sb);

    brelse(bh);

    /* Unlock the inode to make it usable */
    unlock_new_inode(inode);

    return inode;

}

// look for inode in directory with dentry
// return NULL on success, the inode will be connected to the dentry
// if not found connect with NULL
struct dentry *ransomfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags)
{
    struct super_block* sb = parent_inode->i_sb;
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(parent_inode);
    struct inode* inode = NULL;
    int ino;

    AUDIT(TRACE)
    printk(KERN_INFO "Look up called\n");

    //check filename size
    if (child_dentry->d_name.len > RANSOMFS_MAX_FILENAME)
        return ERR_PTR(-ENAMETOOLONG);

    //search for inode in directory
    ino = find_inode_by_name(sb, ci->extent_tree, child_dentry->d_name.name);
    if (ino >= 0) {
        AUDIT(TRACE)
        printk(KERN_INFO "inode for %s found, it is number %d\n", child_dentry->d_name.name, ino);
        //read the new inode from disk
        inode = ransomfs_iget(sb, ino);
    } else {
        AUDIT(TRACE)
        printk(KERN_INFO "inode for %s not found\n", child_dentry->d_name.name);
    }

    //update dentry
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
    ktime_get_ts(&parent_inode->i_atime);
#else
    parent_inode->i_atime = current_time(parent_inode);
#endif
    mark_inode_dirty(parent_inode);
    d_add(child_dentry, inode);

	return NULL;
}

//Allocates block_count new blocks in the closest group to close_group_idx
// return group and block idx with error == 0 on success, on failure the group idx will contain the error code and error will be 1
block_pos_t ransomfs_get_free_blocks(struct super_block* sb, uint32_t close_group_idx, uint32_t block_count) {

    struct buffer_head* bh;
    uint32_t c, distance;
    uint32_t old_val;
    struct ransomfs_sb_info* sbi = RANSOMFS_SB(sb);
    block_pos_t new_block_pos;
    unsigned long* data_bitmap;

    AUDIT(TRACE)
    printk(KERN_INFO "Get free blocks called\n");

    do {

        //find closest group with free space
        c = 0;
        distance = RANSOMFS_GROUPDESC_PER_BLOCK+1;
        while (c < RANSOMFS_GROUPDESC_PER_BLOCK) {  //can be done faster but gdt is very small so not needed i think
            if (sbi->gdt[c].free_blocks_count <= RANSOMFS_BLOCKS_PER_GROUP && sbi->gdt[c].free_blocks_count >= block_count && abs(close_group_idx - c) < distance) {
                distance = abs(close_group_idx - c);
                new_block_pos.group_idx = c;                      
            }
            c++;
        }

        //no space left
        if (distance == RANSOMFS_GROUPDESC_PER_BLOCK+1) {
            new_block_pos.group_idx = -ENOSPC;
            new_block_pos.error = 1;
            return new_block_pos;
        }

        AUDIT(TRACE)
        printk(KERN_INFO "Found space for new file at group %u\n", new_block_pos.group_idx);

        //update GDT in memory
        old_val = __sync_fetch_and_sub(&sbi->gdt[new_block_pos.group_idx].free_blocks_count, block_count);
        if (old_val >= block_count)
            break;

        //someone was faster than us and now we have no space - try to look for another group and fix the gdt
        __sync_fetch_and_add(&sbi->gdt[new_block_pos.group_idx].free_blocks_count, block_count);

    } while (old_val < block_count);

    //load data bitmap from disk
    bh = sb_bread(sb, RANSOMFS_DATA_BITMAP_BLOCK_IDX(new_block_pos.group_idx));
    data_bitmap = (unsigned long*) bh->b_data;
    mutex_lock_interruptible(&sbi->data_bitmap_mutex);

    AUDIT(DEBUG)
    printk("bitmap: %lx", *data_bitmap);

    new_block_pos.block_idx = bitmap_find_next_zero_area(data_bitmap, RANSOMFS_BLOCK_SIZE*8, 0, block_count, 0);

    if (new_block_pos.block_idx >= RANSOMFS_BLOCK_SIZE*8) {
        AUDIT(DEBUG)
        printk(KERN_ERR "FATAL ERROR: Corrupted GDT, found no data block avaible\n"); //should never happen
        __sync_fetch_and_add(&sbi->gdt[new_block_pos.group_idx].free_blocks_count, block_count);
        __sync_fetch_and_add(&sbi->sb->free_blocks_count, block_count);
        new_block_pos.group_idx = -ENOTRECOVERABLE;
        new_block_pos.error = 1;
        mutex_unlock(&sbi->data_bitmap_mutex);
        return new_block_pos;
    }

    AUDIT(TRACE)
    printk(KERN_INFO "Found %d free blocks at index %u\n", block_count, new_block_pos.block_idx);

    //update datablock bitmap
    bitmap_set(data_bitmap, new_block_pos.block_idx, block_count);
    mark_buffer_dirty(bh);
    brelse(bh);
    
    mutex_unlock(&sbi->data_bitmap_mutex);

    //update superblock
    __sync_fetch_and_sub(&sbi->sb->free_blocks_count, block_count);

    return new_block_pos;

}

static int ransomfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct super_block* sb = dir->i_sb;
    struct inode* inode = NULL;
    struct ransomfs_inode_info* new_info, *dir_info;
    struct ransomfs_sb_info* sbi = RANSOMFS_SB(sb);
    unsigned long* data_bitmap, *inode_bitmap;
    struct buffer_head* bh;
    unsigned short curr_space;
    uint32_t ino;
    int ret = 0;
    uint32_t parent_group_idx = RANSOMFS_GROUP_IDX(dir->i_ino);
    uint32_t phys_block_idx = 0, inode_table_idx = 0;
    block_pos_t new_block_pos;
    
    AUDIT(TRACE)
    printk(KERN_INFO "Create called\n");
    
    // Check filename length
    if (strlen(dentry->d_name.name) > RANSOMFS_MAX_FILENAME)
        return -ENAMETOOLONG;

    //check mode
    if (!S_ISDIR(mode) && !S_ISREG(mode) && !S_ISFW(mode) && !S_ISMS(mode)) {
        AUDIT(ERROR)
        printk(KERN_ERR "File type not supported");
        return -EINVAL;
    }

    /*
        Find free data blocks within group
        - Look for RANSOMFS_INITIAL_FILE_SPACE contiguos blocks
        - if we fail lower the number of block until we reach 0
        - if we reach 0 we have no space left
    */
    curr_space = RANSOMFS_INITIAL_FILE_SPACE;
    while (curr_space > 0) {
        new_block_pos = ransomfs_get_free_blocks(sb, parent_group_idx, curr_space);
        if (new_block_pos.error == 0)
            break;
        curr_space--;
    }

    if (curr_space == 0) {
        ret = new_block_pos.group_idx;
        goto success;
    }
    
    //get actual phys block number
    phys_block_idx = RANSOMFS_POS_TO_PHYS(new_block_pos.group_idx,new_block_pos.block_idx);

    //find free space for inode in table
    //we have a more space than needed for inodes, so if we did find a data block we will always find space for an inode
    bh = sb_bread(sb, RANSOMFS_INODE_BITMAP_BLOCK_IDX(new_block_pos.group_idx));
    inode_bitmap = (unsigned long*) bh->b_data;
    mutex_lock_interruptible(&sbi->inode_bitmap_mutex);

    inode_table_idx = bitmap_find_next_zero_area(inode_bitmap, RANSOMFS_BLOCK_SIZE*8, 0, 1, 0);

    AUDIT(TRACE)
    printk(KERN_INFO "Found space for inode at index %u\n", inode_table_idx);

    //update inode bitmap
    bitmap_set(inode_bitmap, inode_table_idx, 1);
    mark_buffer_dirty(bh);
    brelse(bh);

    mutex_unlock(&sbi->inode_bitmap_mutex);

    //down here we don't need to worry about concurrency we have already reserved the zones on the disk that we are going to use
    
    //get the new inode
    ino = RANSOMFS_INODES_PER_GROUP*new_block_pos.group_idx + inode_table_idx;
    inode = ransomfs_iget(sb, ino);
    if (IS_ERR(inode)) {
        ret = PTR_ERR(inode);
        goto correct_bitmaps;
    }

    AUDIT(TRACE)
    printk(KERN_INFO "Got new inode with index %u\n", ino);

    AUDIT(DEBUG)
    printk(KERN_INFO "inode mode %x\n", mode);

    //overwrite file type with the one in the superblock
    mode = (mode & 07777);
    
    AUDIT(DEBUG)
    printk(KERN_INFO "inode mode %x\n", mode);

    mode |= sbi->file_prot_mode;

    AUDIT(DEBUG)
    printk(KERN_INFO "inode mode %x\n", mode);

    //initialize the inode
    inode_init_owner(inode, dir, mode);

    new_info = RANSOMFS_INODE(inode);
    ransomfs_init_extent_tree(new_info, phys_block_idx, curr_space);
    new_info->i_committed = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
    ktime_get_ts(&inode->i_ctime);
    ktime_get_ts(&inode->i_atime);
    ktime_get_ts(&inode->i_mtime);
#else
    inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);
#endif
    inode->i_blocks = 1;
    inode->i_ino = ino;
    if (S_ISDIR(mode)) {
        AUDIT(TRACE)
        printk("Creating a directory\n");
        inode->i_size = RANSOMFS_BLOCK_SIZE;
        inode->i_op = &ransomfs_dir_inode_ops;
        inode->i_fop = &ransomfs_dir_ops;
        set_nlink(inode, 2); // . and ..
    } else if (S_ISREG(mode) || S_ISFW(mode) || S_ISMS(mode)) {
        AUDIT(TRACE)
        printk("Creating a regular file\n");
        inode->i_size = 0;
        inode->i_op = &ransomdfs_file_inode_ops;
        inode->i_fop = &ransomfs_file_ops;
        inode->i_mapping->a_ops = &ransomfs_aops;
        set_nlink(inode, 1);
    }

    AUDIT(TRACE)
    printk(KERN_INFO "Inode inizialized\n");
    
    //add to directory
    dir_info = RANSOMFS_INODE(dir);
    if (add_file_to_directory(sb, dir_info->extent_tree, dentry->d_name.name, ino, (S_ISREG(mode) | S_ISFW(mode) | S_ISMS(mode)) + S_ISDIR(mode)*0x2) == 0) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
        ktime_get_ts(&dir->i_ctime);
        ktime_get_ts(&dir->i_atime);
        ktime_get_ts(&dir->i_mtime);
#else
        dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
#endif
        mark_inode_dirty(inode);
        mark_inode_dirty(dir);
        d_instantiate(dentry, inode);

        //update superblock
        __sync_fetch_and_sub(&sbi->sb->free_inodes_count, 1);

        AUDIT(TRACE)
        printk(KERN_INFO "Inode added to directory - Creation SUCCESS\n");
        goto success;
    }

correct_bitmaps:
    //we failed correct the bitmaps    
    bh = sb_bread(sb, RANSOMFS_INODE_BITMAP_BLOCK_IDX(new_block_pos.group_idx));
    inode_bitmap = (unsigned long*) bh->b_data;
    mutex_lock_interruptible(&sbi->inode_bitmap_mutex);
    bitmap_clear(inode_bitmap, inode_table_idx, 1);
    mark_buffer_dirty(bh);
    brelse(bh);
    mutex_unlock(&sbi->inode_bitmap_mutex);
    
    bh = sb_bread(sb,  RANSOMFS_DATA_BITMAP_BLOCK_IDX(new_block_pos.group_idx));
    data_bitmap = (unsigned long*) bh->b_data;
    mutex_lock_interruptible(&sbi->data_bitmap_mutex);
    bitmap_clear(data_bitmap, new_block_pos.block_idx, curr_space);
    mark_buffer_dirty(bh);
    brelse(bh);
    mutex_unlock(&sbi->data_bitmap_mutex);

    //we failed correct the gdt and superblock
    __sync_fetch_and_add(&sbi->gdt[new_block_pos.group_idx].free_blocks_count, curr_space);
    __sync_fetch_and_add(&sbi->gdt[new_block_pos.group_idx].free_inodes_count, 1);
    __sync_fetch_and_add(&sbi->sb->free_blocks_count, curr_space);
success:
    return ret;
}


static int ransomfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode) {
    return ransomfs_create(dir, dentry, mode | S_IFDIR, 0);
}


static int ransomfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int ret;
	struct inode *inode = d_inode(dentry);
    struct super_block* sb = dir->i_sb;
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(dir);
    struct ransomfs_inode_info *ci_file = RANSOMFS_INODE(inode);

    AUDIT(TRACE)
    printk(KERN_INFO "unlink called\n");

    ret = remove_file_from_directory(sb, ci->extent_tree, inode->i_ino);
    if (ret < 0) {
        AUDIT(ERROR)
        printk(KERN_INFO "Failed to remove file from directory\n");
        return ret;
    }
    
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
    ktime_get_ts(&dir->i_ctime);
    ktime_get_ts(&dir->i_atime);
    ktime_get_ts(&dir->i_mtime);
#else
    dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
#endif
	inode->i_ctime = dir->i_ctime;
    
    if (inode->i_nlink > 1) { //currently there no support for links so this check is pointless
        inode_dec_link_count(inode);
        return 0;
    }

    AUDIT(TRACE)
    printk(KERN_INFO "No links left - deleting file\n");

    ret = ransomfs_free_extent_blocks(sb, ci_file->extent_tree, 0); //free file data blocks
    if (ret < 0) {
        AUDIT(ERROR)
        printk(KERN_INFO "Failed to delete file data blocks\n");
        goto clean_inode; //just delete the inode anyway and lose the blocks forever
    }

    AUDIT(TRACE)
    printk(KERN_INFO "inode unlink success\n");

clean_inode:
    /* Cleanup inode and mark dirty */
    inode->i_blocks = 0;
    inode->i_size = 0;
    i_uid_write(inode, 0);
    i_gid_write(inode, 0);
    inode->i_mode = 0;
    inode->i_ctime.tv_sec = inode->i_mtime.tv_sec = inode->i_atime.tv_sec = 0;
    ci_file->i_committed = 0;
    memset(ci_file->extent_tree, 0, sizeof(struct ransomfs_extent_header)*RANSOMFS_EXTENT_PER_INODE);
    drop_nlink(inode);
    mark_inode_dirty(inode);

	return ret;
}

const struct inode_operations ransomfs_dir_inode_ops = {
    .lookup = ransomfs_lookup,
    .create = ransomfs_create,
    .mkdir = ransomfs_mkdir,
    //.rmdir = ransomfs_rmdir,
};

const struct inode_operations ransomdfs_file_inode_ops = {
    .lookup = ransomfs_lookup,
    .create = ransomfs_create,
    //.unlink = ransomfs_unlink,
};