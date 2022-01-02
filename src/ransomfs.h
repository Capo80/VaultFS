#ifndef RANSOMFS_H
#define RANSOMFS_H

//some debug print helpers
#define DEBUG 1
#define TRACE 1
#define ERROR 1
#define AUDIT(level) 	if(level)

#define RANSOMFS_MAGIC 							0x42424242
#define RANSOMFS_EXTENT_MAGIC					0x1727
#define RANSOMFS_SB_BLOCK_NR 					0
#define RANSOMFS_GDT_BLOCK_NR					1

//Sytem constants e limitations
#define RANSOMFS_BLOCK_SIZE 					(1 << 12) // 4 KB
#define RANSOMFS_BLOCKS_PER_GROUP				32768
#define RANSOMFS_INODES_PER_GROUP				16384
#define RANSOMFS_INODES_GROUP_BLOCK_COUNT 		512
#define RANSOMFS_DIR_RECORD_PER_BLOCK			15
#define RANSOMFS_GROUPDESC_PER_BLOCK			512
#define RANSOMFS_INODES_PER_BLOCK 				32
#define RANSOMFS_EXTENT_PER_INODE				10
#define RANSOMFS_EXTENT_PER_BLOCK				408
#define RANSOMFS_PASSWORD_SIZE					128

#define RANSOMFS_MAX_FILENAME					255
#define RANSOMFS_MAX_FOLDER_FILES				65536
#define RANSOMFS_MAX_DEPTH						5

#define RANSOMFS_INITIAL_FILE_SPACE				4 //number of blocks we would like to leave free after the initial allocation of a file

#pragma pack(2)
struct ransomfs_extent_header {
	uint16_t magic;     //magic number of an extent struct
	uint16_t entries;	//number of entries following the header
	uint16_t max;		//max possible number of entries
	uint16_t depth; 	//depth of the extent tree (if 0 this points to data block)
	uint16_t unused;	//to make it the same length as the other structs
};


#pragma pack(2)
struct ransomfs_extent_idx {
	uint32_t file_block;		//idx of the block that this index starts to cover
	uint32_t leaf_block;		//idx of the block containing the lower level extent
	uint16_t unused;			//to make it the same length as the other structs
};

#pragma pack(2)
struct ransomfs_extent {

	uint32_t file_block;		//idx of the block that this index starts to cover
	uint16_t len;				//number of block covered by this extent
	uint32_t data_block;		//idx of the block containing the data for the file

};

struct ransomfs_inode {
    uint16_t i_mode;   /* File mode */
    uint16_t i_uid;    /* Owner id */
    uint16_t i_gid;    /* Group id */
	uint16_t unused;   /* pad size to 128 */
    uint32_t i_size;   /* Size in bytes */
    uint32_t i_ctime;  /* Inode change time */
    uint32_t i_atime;  /* Access time */
    uint32_t i_mtime;  /* Modification time */
    uint32_t i_blocks; /* Block count */
	struct ransomfs_extent_header extent_tree[RANSOMFS_EXTENT_PER_INODE]; //start of the extent tree
};

struct ransomfs_group_desc {

	uint32_t free_blocks_count;
	uint32_t free_inodes_count;
	uint16_t flags;
	uint16_t unused;			//pad to fill block exactly
	uint32_t unused2;

};

struct ransomfs_dir_record {

	uint32_t ino;								//inode number of this record
	uint16_t name_len;							//len of the filename
	uint8_t file_type;							//type of file (directory, regular file ...)
	char filename[RANSOMFS_MAX_FILENAME];		//filename

};

struct ransomfs_sb_info {

	uint32_t magic;

	uint32_t inodes_count; /* Total inode count */
	uint32_t blocks_count; /* Total block ocunt */

	uint32_t free_inodes_count;
	uint32_t free_blocks_count;

	uint64_t mtime; //mount time - seconds since epoch

	unsigned char passwd_hash[RANSOMFS_PASSWORD_SIZE];

};

typedef struct block_pos {
	uint32_t group_idx;
	uint32_t block_idx;
	char error;
} block_pos_t;

#ifdef __KERNEL__  //prevent errors when including in user mode

static DEFINE_MUTEX(data_bitmap_mutex);
extern struct ransomfs_group_desc* gdt;

struct ransomfs_inode_info {
	struct ransomfs_extent_header extent_tree[RANSOMFS_EXTENT_PER_INODE]; //start of the extent tree
    struct inode vfs_inode;
};

/* dir.c */
int add_file_to_directory(struct super_block* sb, struct ransomfs_extent_header *block_head, const unsigned char* filename, uint32_t ino, uint8_t file_type);


/* super.c */
int ransomfs_fill_super(struct super_block *sb, void *data, int silent);
int ransomfs_init_inode_cache(void);
void ransomfs_destroy_inode_cache(void);

/* inode.c */
struct inode *ransomfs_iget(struct super_block *sb, unsigned long ino);
block_pos_t ransomfs_get_free_blocks(struct super_block* sb, uint32_t close_group_idx, uint32_t block_count);

/* extent.c */
void ransomfs_init_extent_tree(struct ransomfs_inode_info* inode, uint32_t first_block_no);
uint32_t ransomfs_exent_search_block(struct ransomfs_extent_header* block_head, uint32_t logical_block_no);
uint32_t ransomfs_allocate_new_block(struct super_block* sb, struct ransomfs_extent_header* block_head, uint32_t logical_block_no, uint32_t initial_block);

/* oprations */
extern const struct file_operations ransomfs_dir_ops;
extern const struct file_operations ransomfs_file_ops;
extern const struct inode_operations ransomfs_dir_inode_ops;
extern const struct inode_operations ransomdfs_file_inode_ops;
extern const struct address_space_operations ransomfs_aops;

/* conversions */
#define RANSOMFS_POS_TO_PHYS(g_idx, b_idx) \
	2 + (g_idx)*RANSOMFS_BLOCKS_PER_GROUP + (g_idx+1)*(RANSOMFS_INODES_GROUP_BLOCK_COUNT + 2) + b_idx;
#define RANSOMFS_SB(sb) (sb->s_fs_info)
#define RANSOMFS_INODE(inode) \
    (container_of(inode, struct ransomfs_inode_info, vfs_inode))

#endif /* __KERNEL__ */

#endif /* RANSOMFS_H */
