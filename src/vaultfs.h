#ifndef VAULTFS_H
#define VAULTFS_H

//some debug print helpers
#define DEBUG 0
#define TRACE 0
#define ERROR 0
#define WORK  0
#define AUDIT(level) 	if(level)

#define VAULTFS_MAGIC 							0x42424242
#define VAULTFS_EXTENT_MAGIC					0x1727
#define VAULTFS_SB_BLOCK_NR 					0
#define VAULTFS_GDT_BLOCK_NR					1

//Sytem constants e limitations
#define VAULTFS_BLOCK_SIZE 					(1 << 12) // 4 KB
#define VAULTFS_BLOCKS_PER_GROUP				32768
#define VAULTFS_INODES_PER_GROUP				32768
#define VAULTFS_INODES_GROUP_BLOCK_COUNT 		1024
#define VAULTFS_DIR_RECORD_PER_BLOCK			15
#define VAULTFS_GROUPDESC_PER_BLOCK				512
#define VAULTFS_INODES_PER_BLOCK 				32
#define VAULTFS_EXTENT_PER_INODE				10
#define VAULTFS_EXTENT_PER_BLOCK				408
#define VAULTFS_PASSWORD_SIZE					128

#define VAULTFS_MAX_FILENAME					255
#define VAULTFS_MAX_FOLDER_FILES				65536
#define VAULTFS_MAX_DEPTH						5

#define VAULTFS_INITIAL_FILE_SPACE				1 //number of blocks we would like to allocate to a file when we create it


// File flags
#define P_RG 0x00		// regular file, single session, append only
#define P_MS 0x01     	// multiple session file
#define P_FW 0x02     	// free write file


#pragma pack(2)
struct vaultfs_extent_header {
	uint16_t magic;     //magic number of an extent struct
	uint16_t entries;	//number of entries following the header
	uint16_t max;		//max possible number of entries
	uint16_t depth; 	//depth of the extent tree (if 0 this points to data block)
	uint16_t unused;	//to make it the same length as the other structs
};


#pragma pack(2)
struct vaultfs_extent_idx {
	uint32_t file_block;		//idx of the block that this index starts to cover
	uint32_t leaf_block;		//idx of the block containing the lower level extent
	uint16_t unused;			//to make it the same length as the other structs
};

#pragma pack(2)
struct vaultfs_extent {

	uint32_t file_block;		//idx of the block that this index starts to cover
	uint16_t len;				//number of block covered by this extent
	uint32_t data_block;		//idx of the block containing the data for the file

};

struct vaultfs_inode {
    uint16_t i_mode;   			// File mode 
    uint16_t i_uid;    			// Owner id 
    uint16_t i_gid;    			// Group id 
	uint8_t  i_committed;   	// 0 if still writable, 1 if not (only need 1 bit but we have space to spare) 
    uint8_t  i_prot_mode;		// protection mode of the file P_RG, P_MS or P_FW
	uint32_t i_size;   			// Size in bytes 
    uint32_t i_ctime;  			// Inode change time 
    uint32_t i_atime;  			// Access time 
    uint32_t i_mtime;  			// Modification time 
    uint32_t i_blocks; 			// Block count 
	struct vaultfs_extent_header extent_tree[VAULTFS_EXTENT_PER_INODE]; //start of the extent tree
};

struct vaultfs_group_desc {

	uint32_t free_blocks_count;
	uint32_t free_inodes_count;
	uint16_t flags;
	uint16_t unused;			//pad to fill block exactly
	uint32_t unused2;

};

struct vaultfs_dir_record {

	uint32_t ino;								//inode number of this record
	uint16_t name_len;							//len of the filename
	uint8_t file_type;							//type of file (directory, regular file ...)
	char filename[VAULTFS_MAX_FILENAME];		//filename

};

//superblock on disk
struct vaultfs_superblock {

	uint32_t magic;

	uint32_t inodes_count; /* Total inode count */
	uint32_t blocks_count; /* Total block count */

	uint32_t free_inodes_count;
	uint32_t free_blocks_count;

	uint64_t mtime; //mount time - seconds since epoch

	unsigned char passwd_hash[VAULTFS_PASSWORD_SIZE];

};

typedef struct block_pos {
	uint32_t group_idx;
	uint32_t block_idx;
	char error;
} block_pos_t;

#ifdef __KERNEL__  //prevent errors when including in user mode

#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/slab.h>


struct vaultfs_inode_info {
	struct vaultfs_extent_header extent_tree[VAULTFS_EXTENT_PER_INODE]; //start of the extent tree
    struct inode vfs_inode;
	uint8_t i_committed;   	// 0 if still writable, 1 if not (only need 1 bit but we have space to spare) 
	uint8_t i_prot_mode;   	// protection mode of the file P_RG, P_MS or P_FW 
};

//superblock in memory
struct vaultfs_sb_info {

	struct vaultfs_superblock* sb;

	struct vaultfs_group_desc* gdt; //cache the gdt

	uint8_t file_prot_mode; //file protection that is currently used for new files (S_IFFW, S_IFMS or S_IFREG)

	//mutex to sync modification
	struct mutex inode_bitmap_mutex; //TODO theese 2 should be one per group
	struct mutex data_bitmap_mutex;

};

/* dir.c */
int add_file_to_directory(struct super_block* sb, struct vaultfs_extent_header *block_head, const unsigned char* filename, uint32_t ino, uint8_t file_type);
int remove_file_from_directory(struct super_block* sb, struct vaultfs_extent_header *block_head, uint32_t ino);

/* super.c */
int vaultfs_fill_super(struct super_block *sb, void *data, int silent);
int vaultfs_init_inode_cache(void);
void vaultfs_destroy_inode_cache(void);

/* inode.c */
struct inode *vaultfs_iget(struct super_block *sb, unsigned long ino);
block_pos_t vaultfs_get_free_blocks(struct super_block* sb, uint32_t close_group_idx, uint32_t block_count);

/* extent.c */
void vaultfs_init_extent_tree(struct vaultfs_inode_info* inode, uint32_t first_block_no, uint32_t first_node_len);
uint32_t vaultfs_extent_search_block(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t logical_block_no);
uint32_t vaultfs_allocate_new_block(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t logical_block_no, uint32_t initial_block);
uint32_t get_last_logical_block_no(struct super_block* sb, struct vaultfs_extent_header *block_head, uint32_t* last_phys_block);
int vaultfs_free_extent_blocks(struct super_block* sb, struct vaultfs_extent_header* block_head, uint32_t start);

/* oprations */
extern const struct file_operations vaultfs_dir_ops;
extern const struct file_operations vaultfs_file_ops;
extern const struct inode_operations vaultfs_dir_inode_ops;
extern const struct inode_operations ransomdfs_file_inode_ops;
extern const struct address_space_operations vaultfs_aops;

/* conversions */	
#define VAULTFS_SB(sb) (sb->s_fs_info)
#define VAULTFS_INODE(inode) \
    (container_of(inode, struct vaultfs_inode_info, vfs_inode))
#define VAULTFS_GROUP_IDX(b_idx) \
	(b_idx - 2) / VAULTFS_BLOCKS_PER_GROUP
#define VAULTFS_DATA_BITMAP_BLOCK_IDX(g_idx) \
	2 + (g_idx)*VAULTFS_BLOCKS_PER_GROUP
#define VAULTFS_INODE_BITMAP_BLOCK_IDX(g_idx) \
	2 + (g_idx)*VAULTFS_BLOCKS_PER_GROUP + 1
#define VAULTFS_INODE_BLOCK_IDX(g_idx, i_idx) \
	2 + (g_idx)*VAULTFS_BLOCKS_PER_GROUP + 2 + i_idx / VAULTFS_INODES_PER_BLOCK
#define VAULTFS_POS_TO_PHYS(g_idx, b_idx) \
	2 + (g_idx)*VAULTFS_BLOCKS_PER_GROUP + (VAULTFS_INODES_GROUP_BLOCK_COUNT + 2) + b_idx;
#define VAULTFS_PHYS_TO_POS(phys_idx) \
	(block_pos_t) {		\
		.group_idx = VAULTFS_GROUP_IDX(phys_idx),				\
		.block_idx = ((phys_idx - 2) % VAULTFS_BLOCKS_PER_GROUP) - (VAULTFS_INODES_GROUP_BLOCK_COUNT + 2), \
	}	
#endif /* __KERNEL__ */

#endif /* VAULTFS_H */
