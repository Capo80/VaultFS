#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "ransomfs.h"

/*
	Iterate over the files in the folder and pass them to the context.
	This function is called multiple times by the VFS when its changing the ctx pos
*/
static int ransomfs_iterate(struct file *dir, struct dir_context *ctx)
{
    struct inode *inode = file_inode(dir);
    uint32_t total_entries, lbo;
    printk(KERN_INFO "iterate called\n");
    
    // Check that dir is a directory
    if (!S_ISDIR(inode->i_mode))
        return -ENOTDIR;
    
    // . and ..
    if (ctx->pos < 2)
	    if (!dir_emit_dots(dir, ctx)) //FIXME: not working, why?
	        return 0;
    
    //check that we have enough files in this folder 
    total_entries = inode->i_size / sizeof(struct ransomfs_dir_record);
    if (ctx->pos > total_entries + 2)
        return 0;
    
    //find the block that houses the file we are looking for - maybe some caching here?
    lbo = ctx->pos / RANSOMFS_DIR_RECORD_PER_BLOCK; //logical block number

    return 0;
}

const struct file_operations ransomfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = ransomfs_iterate,
};