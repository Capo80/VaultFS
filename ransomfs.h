#ifndef RANSOMFS_H
#define RANSOMFS_H

#define RANSOMFS_MAGIC 0x42424242

#define RANSOMFS_BLOCK_SIZE (1 << 12) /* 4 KiB */

struct simplefs_inode {
    uint32_t i_mode;   /* File mode */
    uint32_t i_uid;    /* Owner id */
    uint32_t i_gid;    /* Group id */
    uint32_t i_size;   /* Size in bytes */
    uint32_t i_ctime;  /* Inode change time */
    uint32_t i_atime;  /* Access time */
    uint32_t i_mtime;  /* Modification time */
    uint32_t i_blocks; /* Block count */
    uint32_t i_nlink;  /* Hard links count */
    union {
        uint32_t ei_block;  /* Block with list of extents for this file */
        uint32_t dir_block; /* Block with list of files for this directory */
    };
    char i_data[32]; /* store symlink content */
}

struct ransomfs_sb_info {

	uint32_t magic;

	uint32_t inodes_count;
	uint32_t blocks_count;

	uint32_t free_inodes_count;
	uint32_t free_blocks_count;

	uint64_t mtime; //mount time - seconds since epoch

}


/* super.c */
int ransomfs_fill_super(struct super_block *sb, void *data, int silent);



/* conversion */
#define RANSOMFS_SB(sb) (sb->s_fs_info)
#define RANSOMFS_INODE(inode) \
    (container_of(inode, struct ransomfs_inode_info, vfs_inode))

#endif /* RANSOMFS_H */
