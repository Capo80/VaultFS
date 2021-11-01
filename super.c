#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/timekeeping.h>

#include "ransomfs.h"



/* Fill the struct superblock from partition superblock */
int ransomfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct buffer_head *bh = NULL;
    struct ransomfs_sb_info *dsb = NULL;
    struct ransomfs_sb_info *rbi = NULL;
    struct inode *root_inode = NULL;
    int ret = 0, i;

    /* Init sb */
    sb->s_magic = RANSOMFS_MAGIC;
    sb->block_size = sb, RANSOMFS_BLOCK_SIZE;
    //sb->s_op = &ransom_super_ops;

    /* Read sb from disk */
    bh = sb_bread(sb, RANSOMFS_SB_BLOCK_NR);
    if (!bh)
        return -EIO;

    dsb = (struct ransomfs_sb_info *) bh->b_data;

    /* Check magic number */
    if (dsb->magic != sb->s_magic) {
        pr_err("Wrong magic number\n");
        ret = -EINVAL;
        goto release;
    }

    /* Alloc sb_info */
    rbi = kzalloc(sizeof(struct ransomfs_sb_info), GFP_KERNEL);
    if (!rbi) {
        ret = -ENOMEM;
        goto release;
    }

    rbi->inodes_count = dsb->inodes_count;
    rbi->blocks_count = dsb->blocks_count;
    rbi->free_inodes_count = dsb->free_inodes_count;
	rbi->free_blocks_count = dsb->free_blocks_count;
	rbi->mtime = ktime_get_real_ns();

    sb->s_fs_info = rbi;

    brelse(bh);

    /* Create root inode */
    root_inode = ransomfs_iget(sb, 0);
    if (IS_ERR(root_inode)) {
        ret = PTR_ERR(root_inode);
        goto free_bfree;
    }
    inode_init_owner(root_inode, NULL, root_inode->i_mode);
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        ret = -ENOMEM;
        goto iput;
    }

    return 0;

iput:
    iput(root_inode);
free_bfree:
    kfree(rbi->bfree_bitmap);
free_ifree:
    kfree(rbi->ifree_bitmap);
free_rbi:
    kfree(rbi);
release:
    brelse(bh);

    return ret;
}