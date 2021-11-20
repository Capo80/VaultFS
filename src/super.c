#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/timekeeping.h>

#include "ransomfs.h"


static struct kmem_cache *ransomfs_inode_cache;

int ransomfs_init_inode_cache(void)
{
    ransomfs_inode_cache = kmem_cache_create("ransomfs_cache", sizeof(struct ransomfs_inode_info), 0, 0, NULL);
    if (!ransomfs_inode_cache)
        return -ENOMEM;
    return 0;
}

void ransomfs_destroy_inode_cache(void)
{
    kmem_cache_destroy(ransomfs_inode_cache);
}

static struct inode *ransomfs_alloc_inode(struct super_block *sb)
{
    struct ransomfs_inode_info *ci = kmem_cache_alloc(ransomfs_inode_cache, GFP_KERNEL);
    if (!ci)
        return NULL;

    inode_init_once(&ci->vfs_inode);
    return &ci->vfs_inode;
}

static void ransomfs_destroy_inode(struct inode *inode)
{
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(inode);
    kmem_cache_free(ransomfs_inode_cache, ci);
}

static int ransomfs_write_inode(struct inode *inode,
                                struct writeback_control *wbc)
{
    struct ransomfs_inode *disk_inode;
    struct ransomfs_inode_info *ci = RANSOMFS_INODE(inode);
    struct super_block *sb = inode->i_sb;
    struct ransomfs_sb_info *sbi = RANSOMFS_SB(sb);
    struct buffer_head *bh;
    uint32_t ino = inode->i_ino;
    uint32_t inode_bg = ino / RANSOMFS_INODES_PER_GROUP;
    uint32_t inode_shift = ino % RANSOMFS_INODES_PER_GROUP;
    uint32_t inode_block = 4 + inode_bg * RANSOMFS_BLOCKS_PER_GROUP + inode_shift / RANSOMFS_INODES_PER_BLOCK;

    if (ino >= sbi->inodes_count)
        return 0;

    bh = sb_bread(sb, inode_block);
    if (!bh)
        return -EIO;

    disk_inode = (struct ransomfs_inode *) bh->b_data;
    disk_inode += inode_shift;

    /* update the mode using what the generic inode has */
    disk_inode->i_mode = inode->i_mode;
    disk_inode->i_uid = i_uid_read(inode);
    disk_inode->i_gid = i_gid_read(inode);
    disk_inode->i_size = inode->i_size;
    disk_inode->i_ctime = inode->i_ctime.tv_sec;
    disk_inode->i_atime = inode->i_atime.tv_sec;
    disk_inode->i_mtime = inode->i_mtime.tv_sec;
    disk_inode->i_blocks = inode->i_blocks;
    memcpy(disk_inode->extent_tree, ci->extent_tree, sizeof(struct ransomfs_extent_header)*RANSOMFS_EXTENT_PER_INODE);

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    return 0;
}


static void ransomfs_put_super(struct super_block *sb)
{
    struct simplefs_sb_info *sbi = RANSOMFS_SB(sb);
    if (sbi) {
        kfree(sbi);
    }
}


static struct super_operations ransomfs_super_ops = {
    .put_super = ransomfs_put_super,
    .alloc_inode = ransomfs_alloc_inode,
    .destroy_inode = ransomfs_destroy_inode,
    .write_inode = ransomfs_write_inode,
    .statfs = simple_statfs,
};

/* Fill the struct superblock from partition superblock */
int ransomfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct buffer_head *bh = NULL;
    struct ransomfs_sb_info *dsb = NULL;
    struct ransomfs_sb_info *rbi = NULL;
    struct inode *root_inode = NULL;
    int ret = 0;

    //Init sb
    sb->s_magic = RANSOMFS_MAGIC;
    sb_set_blocksize(sb, RANSOMFS_BLOCK_SIZE);
    sb->s_op = &ransomfs_super_ops;

    // Read sb from disk
    bh = sb_bread(sb, RANSOMFS_SB_BLOCK_NR);
    if (!bh)
        return -EIO;

    dsb = (struct ransomfs_sb_info *) bh->b_data;

    // Check magic number
    if (dsb->magic != sb->s_magic) {
        pr_err("Wrong magic number\n");
        ret = -EINVAL;
        goto release;
    }

    //fill sb_info 
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

    // set up root inode
    root_inode = ransomfs_iget(sb, 0);
    if (IS_ERR(root_inode)) {
        ret = PTR_ERR(root_inode);
        goto free_rbi;
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
free_rbi:
    kfree(rbi);
release:
    brelse(bh);

    return ret;
}