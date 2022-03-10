#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/timekeeping.h>

#include "vaultfs.h"


static struct kmem_cache *vaultfs_inode_cache;

int vaultfs_init_inode_cache(void)
{
    vaultfs_inode_cache = kmem_cache_create("vaultfs_cache", sizeof(struct vaultfs_inode_info), 0, 0, NULL);
    if (!vaultfs_inode_cache)
        return -ENOMEM;
    return 0;
}

void vaultfs_destroy_inode_cache(void)
{
    kmem_cache_destroy(vaultfs_inode_cache);
}

static struct inode *vaultfs_alloc_inode(struct super_block *sb)
{
    struct vaultfs_inode_info *ci = kmem_cache_alloc(vaultfs_inode_cache, GFP_KERNEL);
    if (!ci)
        return NULL;

    inode_init_once(&ci->vfs_inode);
    return &ci->vfs_inode;
}

static void vaultfs_destroy_inode(struct inode *inode)
{
    struct vaultfs_inode_info *ci = VAULTFS_INODE(inode);
    if (ci != NULL)    
        kmem_cache_free(vaultfs_inode_cache, ci);
}

static int vaultfs_write_inode(struct inode *inode,
                                struct writeback_control *wbc)
{
    struct vaultfs_inode *disk_inode;
    struct vaultfs_inode_info *ci = VAULTFS_INODE(inode);
    struct super_block *sb = inode->i_sb;
    struct vaultfs_sb_info *sbi = VAULTFS_SB(sb);
    struct buffer_head *bh;
    uint32_t ino = inode->i_ino;
    uint32_t inode_bg = ino / VAULTFS_INODES_PER_GROUP;
    uint32_t inode_shift = ino % VAULTFS_INODES_PER_GROUP;
    uint32_t inode_block = VAULTFS_INODE_BLOCK_IDX(inode_bg, inode_shift);
    uint32_t inode_block_shift = inode_shift % VAULTFS_INODES_PER_BLOCK;

    AUDIT(TRACE)
    printk("Write inode called\n");
    AUDIT(TRACE)
    printk("\tino: %u\n", ino);
    AUDIT(TRACE)
    printk("\tblock: %u\n", inode_block);
    AUDIT(TRACE)
    printk("\tshift: %u\n", inode_shift);
    AUDIT(TRACE)
    printk("\tshift: %u\n", inode_block_shift);
    
    if (ino >= sbi->sb->inodes_count)
        return 0;

    bh = sb_bread(sb, inode_block);
    if (!bh)
        return -EIO;

    disk_inode = (struct vaultfs_inode *) bh->b_data;
    disk_inode += inode_block_shift;

    /* update the mode using what the generic inode has */
    disk_inode->i_mode = inode->i_mode;
    disk_inode->i_uid = i_uid_read(inode);
    disk_inode->i_gid = i_gid_read(inode);
    disk_inode->i_committed = ci->i_committed;
    disk_inode->i_prot_mode = ci->i_prot_mode;
    disk_inode->i_size = inode->i_size;
    disk_inode->i_ctime = inode->i_ctime.tv_sec;
    disk_inode->i_atime = inode->i_atime.tv_sec;
    disk_inode->i_mtime = inode->i_mtime.tv_sec;
    disk_inode->i_blocks = inode->i_blocks;
    memcpy(disk_inode->extent_tree, ci->extent_tree, sizeof(struct vaultfs_extent_header)*VAULTFS_EXTENT_PER_INODE);

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    return 0;
}


static void vaultfs_put_super(struct super_block *sb)
{
    struct vaultfs_sb_info *sbi = VAULTFS_SB(sb);
    if (sbi) {
        if (sbi->gdt)
            kfree(sbi->gdt);
        if (sbi->sb)
            kfree(sbi->sb);
        kfree(sbi);
    }
}

static int vaultfs_statfs(struct dentry *dentry, struct kstatfs *stat)
{
    struct super_block *sb = dentry->d_sb;
    struct vaultfs_sb_info *sbi = VAULTFS_SB(sb);

    stat->f_type = VAULTFS_MAGIC;
    stat->f_bsize = VAULTFS_BLOCK_SIZE;
    stat->f_blocks = sbi->sb->blocks_count;
    stat->f_bfree = sbi->sb->free_blocks_count;
    stat->f_bavail = sbi->sb->free_blocks_count;
    stat->f_files = sbi->sb->inodes_count - sbi->sb->free_inodes_count;
    stat->f_ffree = sbi->sb->free_inodes_count;
    stat->f_namelen = VAULTFS_MAX_FILENAME;

    return 0;
}

static int vaultfs_sync_fs(struct super_block *sb, int wait)
{
    struct vaultfs_sb_info *sbi = VAULTFS_SB(sb);
    struct vaultfs_superblock *disk_sb;

    // save super block
    struct buffer_head *bh = sb_bread(sb, VAULTFS_SB_BLOCK_NR);
    if (!bh)
        return -EIO;

    AUDIT(DEBUG)
    printk(KERN_INFO"syncfs called\n");   

    disk_sb = (struct vaultfs_superblock *) bh->b_data;

    disk_sb->blocks_count = sbi->sb->blocks_count;
    disk_sb->inodes_count = sbi->sb->inodes_count;
    disk_sb->free_inodes_count = sbi->sb->free_inodes_count;
    disk_sb->free_blocks_count = sbi->sb->free_blocks_count;

    mark_buffer_dirty(bh);
    if (wait)
        sync_dirty_buffer(bh);
    brelse(bh);

    //update GDT on disk
    bh = sb_bread(sb, VAULTFS_GDT_BLOCK_NR);
    memcpy(bh->b_data, sbi->gdt, VAULTFS_BLOCK_SIZE);
    mark_buffer_dirty(bh);                             
    brelse(bh);
    
    return 0;
}


static struct super_operations vaultfs_super_ops = {
    .put_super = vaultfs_put_super,
    .alloc_inode = vaultfs_alloc_inode,
    .destroy_inode = vaultfs_destroy_inode,
    .write_inode = vaultfs_write_inode,
    .sync_fs = vaultfs_sync_fs,
    .statfs = vaultfs_statfs,
};

/* Fill the struct superblock from partition superblock */
int vaultfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct buffer_head *bh = NULL;
    struct vaultfs_superblock *dsb = NULL;
    struct vaultfs_sb_info *rbi = NULL;
    struct inode *root_inode = NULL;
    int ret = 0;

    //Init sb
    sb->s_magic = VAULTFS_MAGIC;
    sb_set_blocksize(sb, VAULTFS_BLOCK_SIZE);
    sb->s_op = &vaultfs_super_ops;

    // Read sb from disk
    bh = sb_bread(sb, VAULTFS_SB_BLOCK_NR);
    if (!bh)
        return -EIO;

    dsb = (struct vaultfs_superblock*) bh->b_data;

    // Check magic number
    if (dsb->magic != sb->s_magic) {
        AUDIT(ERROR)
        printk(KERN_ERR"Wrong magic number\n");
        ret = -EINVAL;
        goto release;
    }

    //alloc new superblock 
    rbi = kzalloc(sizeof(struct vaultfs_sb_info), GFP_KERNEL);
    if (!rbi) {
        ret = -ENOMEM;
        goto release;
    }

    rbi->sb = kzalloc(VAULTFS_BLOCK_SIZE, GFP_KERNEL);
    rbi->sb->inodes_count = dsb->inodes_count;
    rbi->sb->blocks_count = dsb->blocks_count;
    rbi->sb->free_inodes_count = dsb->free_inodes_count;
	rbi->sb->free_blocks_count = dsb->free_blocks_count;
	rbi->sb->mtime = ktime_get_real_ns();

    //load the password and pass it to the caller
    memcpy(rbi->sb->passwd_hash, dsb->passwd_hash, VAULTFS_PASSWORD_SIZE);
    memcpy(data, rbi->sb->passwd_hash, VAULTFS_PASSWORD_SIZE);
    
    brelse(bh);

    //load the gdt
    bh = sb_bread(sb, VAULTFS_GDT_BLOCK_NR);
    if (!bh){
        ret = -EIO;
        goto free_rbi;
    }

    rbi->gdt = kzalloc(VAULTFS_BLOCK_SIZE, GFP_KERNEL);
    memcpy(rbi->gdt, bh->b_data, VAULTFS_BLOCK_SIZE);        
    
    rbi->inode_bitmap_mutex = (struct mutex) __MUTEX_INITIALIZER(rbi->inode_bitmap_mutex);
    rbi->data_bitmap_mutex = (struct mutex) __MUTEX_INITIALIZER(rbi->data_bitmap_mutex);

    //init mutex
    mutex_init(&rbi->inode_bitmap_mutex);
    mutex_init(&rbi->data_bitmap_mutex);

    //set regular protection level
    rbi->file_prot_mode = P_RG;

    sb->s_fs_info = rbi;

    // set up root inode
    root_inode = vaultfs_iget(sb, 0);
    if (IS_ERR(root_inode)) {
        ret = PTR_ERR(root_inode);
        goto free_gdt;
    }

    inode_init_owner(root_inode, NULL, root_inode->i_mode);
    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root) {
        ret = -ENOMEM;
        goto iput;
    }

    brelse(bh);
    return 0;

iput:
    iput(root_inode);
free_gdt:
    kfree(rbi->gdt);
free_rbi:
    kfree(rbi);
release:
    brelse(bh);
    return ret;
}