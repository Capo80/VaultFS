#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpage.h>
#include <linux/version.h>

#include "vaultfs.h"

//map buffer head to i-block of a file, allocate a new one only if create is true
static int vaultfs_file_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create) {

    uint32_t phys_block_no;
    struct super_block *sb = inode->i_sb;
    struct vaultfs_inode_info *ci = VAULTFS_INODE(inode);
    uint32_t inode_bg = inode->i_ino / VAULTFS_INODES_PER_GROUP;
    //TODO what is the max file size?

    AUDIT(TRACE)
    printk(KERN_INFO "Mapping requested\n");

    phys_block_no = vaultfs_extent_search_block(sb, ci->extent_tree, iblock);
    if (phys_block_no == 0) {
        //block not allocated
        if (!create)
            return 0;

        phys_block_no = vaultfs_allocate_new_block(sb, ci->extent_tree, iblock, inode_bg);
        if (phys_block_no < 0)
            return phys_block_no;

        //update inode on disk
        mark_inode_dirty(inode);

    }

    //block already allocated, map it
    map_bh(bh_result, sb, phys_block_no);

    AUDIT(TRACE)
    printk(KERN_INFO "Mapping complete %d\n", phys_block_no);
    return 0;

}

//called by the cache to write and read pages to memory
static int vaultfs_readpage(struct file *file, struct page *page) {
    return mpage_readpage(page, vaultfs_file_get_block);
}
static int vaultfs_writepage(struct page *page, struct writeback_control *wbc) {
    return block_write_full_page(page, vaultfs_file_get_block, wbc);
}

//called before on th ewrite syscall
//check if the write is possible - allocate blocks if necessary 
static int vaultfs_write_begin(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len, unsigned int flags, struct page **pagep, void **fsdata) {

    struct inode* inode = file->f_inode;
    struct vaultfs_inode_info* rsi = VAULTFS_INODE(inode);
    struct vaultfs_sb_info *sbi = VAULTFS_SB(file->f_inode->i_sb);
    uint32_t new_blocks_needed = 0;
    int err;
    
    AUDIT(TRACE)
    printk(KERN_INFO "Write begin called, original size: %lld\n", inode->i_size);

    AUDIT(WORK)
    printk(KERN_INFO "mode: %x", inode->i_mode);

    //check if we can overwrite data 
    if (rsi->i_prot_mode != P_FW && inode->i_size > pos) {
        AUDIT(ERROR)
        printk(KERN_ERR "Invalid write position %lld > %lld\n", inode->i_size, pos);
        return -EINVAL;
    }

    //if (pos + len > VAULTFS_MAX_FILESIZE)
    //    return -ENOSPC;
    
    //check we have enough block left on the disk
    new_blocks_needed = ((pos + len) / VAULTFS_BLOCK_SIZE) + 1;
    if (new_blocks_needed > file->f_inode->i_blocks)
        new_blocks_needed -= file->f_inode->i_blocks;
    else
        new_blocks_needed = 0;

    if (new_blocks_needed  > sbi->sb->free_blocks_count)
        return -ENOSPC;
    
    AUDIT(DEBUG)
    printk(KERN_INFO "Checks passed %llu - %d\n", pos, len);

    //allocate the blocks needed
    err = block_write_begin(mapping, pos, len, flags, pagep, vaultfs_file_get_block);
    if (err < 0) {
        //TODO blocks deallocation
        AUDIT(TRACE)
        printk(KERN_ERR"Something went wrong - blocks need to be deallocated\n");
    }
    
    AUDIT(TRACE)
    printk(KERN_INFO "Begin complete\n");

    return err;
}

//called when the write is finished
//update the inode metadata - truncate the file if necessary (yes, truncation can happen in this filesystem if it is in the same session)
static int vaultfs_write_end(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len, unsigned int copied, struct page *page, void *fsdata) {

    struct inode *inode = file->f_inode;
    struct super_block* sb = inode->i_sb;
    struct vaultfs_inode_info *ci = VAULTFS_INODE(inode);
    uint32_t nr_blocks_before, last_block;
    int ret, ret2;

    //do the common filesystem stuff
    ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
    if (ret < len) {
        AUDIT(TRACE)
        printk(KERN_ERR "Could not write all the requested bytes");
        return ret;
    }

    AUDIT(TRACE)
    printk(KERN_INFO "checking if truncation is needed");
    
    nr_blocks_before = inode->i_blocks;

    //update metadata
    inode->i_blocks = inode->i_size / VAULTFS_BLOCK_SIZE + 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,9,0)
    ktime_get_ts(&inode->i_mtime);
    ktime_get_ts(&inode->i_ctime);
#else
    inode->i_mtime = inode->i_ctime = current_time(inode);
#endif
    mark_inode_dirty(inode);

    if (nr_blocks_before > inode->i_blocks) {

        AUDIT(TRACE)
        printk(KERN_INFO "File is smaller - some blocks need to be freed");

        last_block = get_last_logical_block_no(sb, ci->extent_tree, NULL);
        if (last_block < 0) {
            AUDIT(ERROR)
            printk(KERN_ERR"Failed to find last block - lost forever\n");
            return ret;
        }

        //free extra blocks
        ret2 = vaultfs_free_extent_blocks(sb, ci->extent_tree, last_block - (nr_blocks_before - inode->i_blocks) + 1);
        if (ret2 < 0) {
            AUDIT(ERROR)
            printk(KERN_ERR"Failed to free blocks - lost forever\n");
            return ret;
        }

        AUDIT(TRACE)
        printk(KERN_INFO "Block freed succeffuly");
    }

    AUDIT(TRACE)
    printk(KERN_INFO "Write end complete\n");

    return ret;
}

static int vaultfs_file_open(struct inode *inode, struct file *filp) {

    struct vaultfs_inode_info *rsi = VAULTFS_INODE(inode);
    
    AUDIT(TRACE)
    printk(KERN_INFO "open called\n");

    //check if we can write
    if (rsi->i_prot_mode != P_MS && (filp->f_mode & FMODE_WRITE)) {
        if (rsi->i_committed == 1) {
            //failed - already committed
            return -EINVAL;
        } else {
            //success - first access
            //rsi->i_committed = 1;
            if (!__sync_bool_compare_and_swap(&rsi->i_committed, 0, 1)) {
               //somebody else swapped faster than us - fail
               return -EINVAL;
            }
        }
    }
    return generic_file_open(inode, filp);
}

static sector_t vaultfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, vaultfs_file_get_block);
}


const struct address_space_operations vaultfs_aops = {
	.set_page_dirty	= __set_page_dirty_buffers,
    .readpage = vaultfs_readpage,
    .writepage = vaultfs_writepage,
    .write_begin = vaultfs_write_begin,
    .write_end = vaultfs_write_end,
    .bmap = vaultfs_bmap
};

const struct file_operations vaultfs_file_ops = {
    .llseek = generic_file_llseek,
    .owner = THIS_MODULE,
    .read_iter = generic_file_read_iter,
    .open = vaultfs_file_open,
    .write_iter = generic_file_write_iter,
    .fsync = generic_file_fsync,
    .mmap = generic_file_mmap,
};