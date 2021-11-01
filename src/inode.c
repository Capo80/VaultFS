#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "ransomfs.h"

/* Get inode ino from disk */
struct inode *ransomfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode = NULL;
    struct ransomfs_inode *cinode = NULL;
    struct ransomfs_inode_info *ci = NULL;
    struct ransomfs_sb_info *sbi = RANSOMFS_SB(sb);
    struct buffer_head *bh = NULL;
    uint64_t inode_bg = ino / RANSOMFS_INODES_PER_GROUP;
    uint64_t inode_shift = ino % RANSOMFS_INODES_PER_GROUP;
    uint64_t inode_block = 3 + sbi->group_table_blocks_count + inode_bg * RANSOMFS_BLOCKS_PER_GROUP + inode_shift / RANSOMFS_INODES_PER_BLOCK;
    int ret;

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

    inode->i_mode = le32_to_cpu(cinode->i_mode);
    i_uid_write(inode, le32_to_cpu(cinode->i_uid));
    i_gid_write(inode, le32_to_cpu(cinode->i_gid));
    inode->i_size = le32_to_cpu(cinode->i_size);
    inode->i_ctime.tv_sec = (time64_t) le32_to_cpu(cinode->i_ctime);
    inode->i_ctime.tv_nsec = 0;
    inode->i_atime.tv_sec = (time64_t) le32_to_cpu(cinode->i_atime);
    inode->i_atime.tv_nsec = 0;
    inode->i_mtime.tv_sec = (time64_t) le32_to_cpu(cinode->i_mtime);
    inode->i_mtime.tv_nsec = 0;
    inode->i_blocks = le32_to_cpu(cinode->i_blocks);

    if (S_ISDIR(inode->i_mode)) {
        //ci->dir_block = le32_to_cpu(cinode->dir_block);
        //inode->i_fop = &ransomfs_dir_ops;
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
