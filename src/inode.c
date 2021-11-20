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

// recursive function that reads the extent tree and finds an inode number
// TODO every fucntion that reads the exent tree is the same, is there a better way to do this?
// returns the inode number if found,  anegative number if not
static int find_inode_by_name(struct super_block* sb, struct ransomfs_extent_header *block_head, const char* to_search) {

    int i = 0, j = 0, off = 0;
    struct buffer_head *bh = NULL;

    printk(KERN_INFO "Started the search for an inode\n");
    return -1;
    
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
                    if (curr_record->ino != 0)
                        if (strcmp(to_search, curr_record->filename) == 0) {
                            brelse(bh);
                            return curr_record->ino;
                        }
                    curr_record++;
                    off += sizeof(struct ransomfs_dir_record);
                    break;
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

    printk(KERN_INFO "Reding inode from disk\n");

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
    inode->i_op = &ransomfs_inode_ops;

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

    printk(KERN_INFO "Saved disk info on inode\n");

    //copy extent tree
    memcpy(ci->extent_tree, cinode->extent_tree, sizeof(struct ransomfs_extent_header)*RANSOMFS_EXTENT_PER_INODE);

    if (S_ISDIR(inode->i_mode)) {
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
// if not found connect with NULL
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
        //read the new inode from disk
        inode = ransomfs_iget(sb, ino);
    } else
        printk(KERN_INFO "inode for %s not found\n", child_dentry->d_name.name);

    //update dentry
    parent_inode->i_atime = current_time(parent_inode);
    mark_inode_dirty(parent_inode);
    d_add(child_dentry, inode);

	return NULL;
}

static int ransomfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    struct super_block* sb = dir->i_sb;
    struct inode* inode, disk_inode;
    unsigned long* data_bitmap, *inode_bitmap;
    struct buffer_head* bh;
    unsigned short curr_space;
    uint32_t ino;
    int ret;
    uint32_t group_idx, distance = RANSOMFS_GROUPDESC_PER_BLOCK+1, c;
    uint32_t parent_group_idx = dir->i_ino / RANSOMFS_INODES_PER_GROUP;
    uint32_t data_block_idx, inode_table_idx, inode_bno, inode_shift;
    
    printk(KERN_INFO "Create called\n");
    
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
        c++;
    }

    printk(KERN_INFO "Found space for new file at group %u\n", group_idx);
    
    //no space left
    if (distance == RANSOMFS_GROUPDESC_PER_BLOCK+1)
        return -ENOSPC;

    //update GDT in memory
    gdt[c].free_blocks_count--;                //FIXME need concurrency checks here (just a __sync with a check on 0)
    gdt[c].free_inodes_count--;

    //update GDT on disk
    bh = sb_bread(sb, RANSOMFS_GDT_BLOCK_NR);
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

    printk("bitmap: %lx", *data_bitmap);

    curr_space = RANSOMFS_INITIAL_FILE_SPACE;
    while ( curr_space > 0  && (data_block_idx = bitmap_find_next_zero_area(data_bitmap, RANSOMFS_BLOCK_SIZE, 0, curr_space, 0)) >= RANSOMFS_BLOCK_SIZE)
        curr_space--;

    printk(KERN_INFO "Found %d free blocks at index %u\n", curr_space, data_block_idx);

    if (curr_space == 0) {
        printk(KERN_ERR "FATAL ERROR: Corrupted GDT, found no data block avaible\n"); //should never happen
        ret = -ENOSPC;
        goto correct_gdt;
    }

    //update datablock bitmap
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

    printk(KERN_INFO "Found space for inode at index %u\n", inode_table_idx);

    //update inode bitmap
    bitmap_fill(inode_bitmap + inode_table_idx, 1);
    mark_buffer_dirty(bh);
    brelse(bh);

    mutex_unlock(&inode_bitmap_mutex);

    //down here we don't need to worry about concurrency we have already reserved the zones on the disk that we are going to use
    
    //get the new inode
    ino = RANSOMFS_INODES_PER_GROUP*group_idx + inode_table_idx;
    inode = ransomfs_iget(sb, ino);
    if (IS_ERR(inode)) {
        ret = PTR_ERR(inode);
        goto correct_bitmaps;
    }

    printk(KERN_INFO "Got new inode with index %u\n", ino);

    //initialize the inode
    inode_init_owner(inode, dir, mode);
    init_extent_tree((struct ransomfs_inode_info*) inode, group_idx*RANSOMFS_BLOCKS_PER_GROUP+data_block_idx);
    inode->i_ctime = inode->i_atime = inode->i_mtime = current_time(inode);
    inode->i_blocks = 1;
    if (S_ISDIR(mode)) {
        inode->i_size = RANSOMFS_BLOCK_SIZE;
        printk("size\n");
        inode->i_fop = &ransomfs_dir_ops;
        set_nlink(inode, 2); // . and ..
    } else if (S_ISREG(mode)) {
        inode->i_size = 0;
        //node->i_fop = &ransomfs_file_ops
        //inode->i_mapping->a_ops = &ransomfs_aops;
        set_nlink(inode, 1);
    }

    printk(KERN_INFO "Inode inizialized\n");

    //save inode to disk
    //inode_bg = inode_table_idx / RANSOMFS_INODES_PER_BLOCK;
    //inode_shift = inode_table_idx % RANSOMFS_INODES_PER_BLOCK;
    //bh = sb_bread(sb, 2 + group_idx*RANSOMFS_BLOCKS_PER_GROUP+2 + inode_bg);
    //disk_inode = (struct inode*) bh_>b_data;
    //memcpy(disk_inode + inode_shift, inode, sizeof(struct inode))
    
    //add to directory
    if (add_file_to_directory(sb, RANSOMFS_INODE(dir)->extent_tree, dentry->d_name.name, ino, S_ISREG(mode) + S_ISDIR(mode)*0x2) == 0) {
        dir->i_mtime = dir->i_atime = dir->i_ctime = current_time(dir);
        mark_inode_dirty(inode);
        mark_inode_dirty(dir);
        d_instantiate(dentry, inode);
        printk(KERN_INFO "Inode added to directory - Creation SUCCESS\n");
        goto success;
    }
correct_bitmaps:
    //we failed correct the bitmaps
    bitmap_zero(inode_bitmap + inode_table_idx, 1);
    bitmap_zero(data_bitmap + data_block_idx, 1);
correct_gdt:
    //we failed correct the gdt
    gdt[c].free_blocks_count++;                //FIXME need concurrency checks here
    gdt[c].free_inodes_count++;
success:
    iput(inode);
    iput(dir);
    return ret;
}

const struct inode_operations ransomfs_inode_ops = {
    .lookup = ransomfs_lookup,
    .create = ransomfs_create,
};