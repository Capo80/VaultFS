#ifndef RANSOMFS_H
#define RANSOMFS_H

#define RANSOMFS_MAGIC 					0x42424242

#define RANSOMFS_SB_BLOCK_NR 			0

#define RANSOMFS_BLOCK_SIZE 			(1 << 12) /* 4 KiB */
#define RANSOMFS_BLOCKS_PER_GROUP		32768
#define RANSOMFS_INODES_PER_GROUP		32768

struct ransomfs_inode {
    uint32_t i_mode;   /* File mode */
    uint32_t i_uid;    /* Owner id */
    uint32_t i_gid;    /* Group id */
    uint64_t i_size;   /* Size in bytes */
    uint32_t i_ctime;  /* Inode change time */
    uint32_t i_atime;  /* Access time */
    uint32_t i_mtime;  /* Modification time */
    uint64_t i_blocks; /* Block count */
};

#define RANSOMFS_INODES_PER_BLOCK		RANSOMFS_BLOCK_SIZE / sizeof(struct ransomfs_inode)

struct ransomfs_group_desc {

	uint64_t free_blocks_count;
	uint32_t free_inodes_count;
	uint16_t flags;
	uint16_t padding;

};

struct ransomfs_sb_info {

	uint32_t magic;

	uint64_t inodes_count; /* Total inode count */
	uint64_t blocks_count; /* Total block ocunt */

	uint64_t free_inodes_count;
	uint64_t free_blocks_count;

	uint64_t group_table_blocks_count; /* size of the group descriptor table in blocks */

	uint64_t mtime; //mount time - seconds since epoch

};

#ifdef __KERNEL__

struct ransomfs_inode_info {
    struct inode vfs_inode;
};


/* super.c */
int ransomfs_fill_super(struct super_block *sb, void *data, int silent);

/* inode.c */
struct inode *ransomfs_iget(struct super_block *sb, unsigned long ino);

/* conversion */
#define RANSOMFS_SB(sb) (sb->s_fs_info)
#define RANSOMFS_INODE(inode) \
    (container_of(inode, struct ransomfs_inode_info, vfs_inode))

#endif /* __KERNEL__ */

#endif /* RANSOMFS_H */
