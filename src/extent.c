#include <linux/fs.h>
#include <linux/kernel.h>

#include "ransomfs.h"

void init_extent_tree(struct ransomfs_inode_info* inode, uint32_t first_block_no) {

    struct ransomfs_extent_header head;
    struct ransomfs_extent leaf;

    //tree head
    memset(&head, 0, sizeof(struct ransomfs_extent_header));
    head.magic = RANSOMFS_EXTENT_MAGIC;
    head.entries = 1;
    head.max = RANSOMFS_EXTENT_PER_INODE;
    head.depth = 0;
       
    //the only leaf
    memset(&leaf, 0, sizeof(struct ransomfs_extent_header));
    leaf.file_block = 0;
    leaf.len = 1;
    leaf.data_block = first_block_no;

    //save to inode
    memcpy(inode->extent_tree, &head, sizeof(struct ransomfs_extent_header));
    memcpy(inode->extent_tree + 1, &leaf, sizeof(struct ransomfs_extent_header));

}