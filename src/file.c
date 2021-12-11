#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mpage.h>

#include "ransomfs.h"

//map buffer head to i-block of a file, allocate a new one only if create is true
static int ransomfs_file_get_block(struct inode *inode, sector_t iblock, struct buffer_head *bh_result, int create) {

    uint32_t phys_block_no;
    struct super_block *sb = inode->i_sb;
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(inode);

    //TODO what is the max file size?

    printk(KERN_INFO "Mapping requested\n");

    phys_block_no = ransomfs_exent_search_block(ci->extent_tree, iblock);
    if (phys_block_no == 0) {
        //block not allocated
        printk(KERN_INFO "Block %lld not allocated\n", iblock);
        if (!create)
            return 0;

        return ransomfs_allocate_new_block(sb, ci->extent_tree, iblock);

    }

    printk(KERN_INFO "Block %lld allocated, mapping with %d in progess... %ld\n", iblock, phys_block_no, sb->s_blocksize);
    //block already allocated, map it
    map_bh(bh_result, sb, phys_block_no);

    printk(KERN_INFO "Mapping complete\n");
    return 0;

}

//called by the cache to write and read pages to memory
static int ransomfs_readpage(struct file *file, struct page *page) {
    return block_read_full_page(page, ransomfs_file_get_block);
}
static int ransomfs_writepage(struct page *page, struct writeback_control *wbc) {
    return block_write_full_page(page, ransomfs_file_get_block, wbc);
}

//called before on th ewrite syscall
//check if the write is possible - allocate blocks if necessary 
static int ransomfs_write_begin(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len, unsigned int flags, struct page **pagep, void **fsdata) {

    struct ransomfs_sb_info *sbi = RANSOMFS_SB(file->f_inode->i_sb);
    uint32_t new_blocks_needed = 0;
    int err;
    
    printk(KERN_INFO "Write begin called\n");

    if (pos + len > RANSOMFS_MAX_FILESIZE)
        return -ENOSPC;
    
    //check we have enough block left on the disk
    new_blocks_needed = ((pos + len) / RANSOMFS_BLOCK_SIZE) + 1;
    if (new_blocks_needed > file->f_inode->i_blocks)
        new_blocks_needed -= file->f_inode->i_blocks;
    else
        new_blocks_needed = 0;

    if (new_blocks_needed  > sbi->free_blocks_count)
        return -ENOSPC;
    

    printk(KERN_INFO "Checks passed %llu - %d\n", pos, len);

    printk(KERN_INFO "params %p, %p, %llu, %d, %d, %p, %p\n", file, mapping, pos, len, flags, pagep, fsdata);

    printk(KERN_INFO "page %p\n", *pagep);
    //allocate the blocks needed
    err = block_write_begin(mapping, pos, len, flags, pagep, ransomfs_file_get_block);
    if (err < 0)
        //TODO blocks deallocation
        printk(KERN_ERR"Something went wrong - blocks need to be deallocated\n");
    

    printk(KERN_INFO "page %p\n", *pagep);
    printk(KERN_INFO "Begin complete\n");

    return err;
}

//called when the write is finished
//update the inode metadata - truncate the file if necessary (yes, truncation can happen in this filesystem if it is in the same session)
static int ransomfs_write_end(struct file *file, struct address_space *mapping, loff_t pos, unsigned int len, unsigned int copied, struct page *page, void *fsdata) {

    struct inode *inode = file->f_inode;    
    uint32_t nr_blocks_before;
    int ret;

    printk(KERN_INFO "Write end called\n");
    printk(KERN_INFO "params %p, %p, %llu, %d, %d, %p, %p\n", file, mapping, pos, len, copied, page, fsdata);
    //do the common filesystem stuff
    ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
    if (ret < len) {
        printk(KERN_ERR "Could not write all the requested bytes");
        return ret;
    }

    printk(KERN_INFO "checking if truncation is needed");
    
    nr_blocks_before = inode->i_blocks;

    //update metadata
    inode->i_blocks = inode->i_size / RANSOMFS_BLOCK_SIZE + 1;
    inode->i_mtime = inode->i_ctime = current_time(inode);
    mark_inode_dirty(inode);

    if (nr_blocks_before > inode->i_blocks) {
        printk(KERN_INFO "File is smaller - some blocks need to be freed");
    }

    printk(KERN_INFO "Write end complete\n");

    return ret;
}

static int ransomfs_file_open(struct inode *inode, struct file *filp) {

    printk(KERN_INFO "open called\n");

    //TODO here we implement write protection

	return generic_file_open(inode, filp);
}

static sector_t ransomfs_bmap(struct address_space *mapping, sector_t block)
{
	return generic_block_bmap(mapping, block, ransomfs_file_get_block);
}


const struct address_space_operations ransomfs_aops = {
	.set_page_dirty	= __set_page_dirty_buffers,
    .readpage = ransomfs_readpage,
    .writepage = ransomfs_writepage,
    .write_begin = ransomfs_write_begin,
    .write_end = ransomfs_write_end,
    .bmap = ransomfs_bmap
};

const struct file_operations ransomfs_file_ops = {
    .llseek = generic_file_llseek,
    .owner = THIS_MODULE,
    .read_iter = generic_file_read_iter,
    //.open = ransomfs_file_open,
    .write_iter = generic_file_write_iter,
    .fsync = generic_file_fsync,
    .mmap = generic_file_mmap,
};