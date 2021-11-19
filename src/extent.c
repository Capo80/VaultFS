#include <linux/fs.h>
#include <linux/kernel.h>

#include "ransomfs.h"

void init_extent_tree(struct ransomfs_inode* inode, uint32_t first_block_no) {

    //tree head
    struct ransomfs_extent_header head = {
        .magic = RANSOMFS_EXTENT_MAGIC,
        .entries = 1,
        .max = RANSOMFS_EXTENT_PER_INODE,
        .depth = 1
    };

    //only leaf
    struct ransomfs_extent leaf = {
        .file_block = 0,
        .len = 1,
        .data_block = first_block_no
    };

    //zero the memory
    memset(inode->extent_tree, 0, sizeof(struct ransomfs_extent_header)*RANSOMFS_EXTENT_PER_INODE);

    //save to inode
    inode->extent_tree[0] = head;
    inode->extent_tree[1] = *((struct ransomfs_extent_header*) &leaf); //is this the best wayt to do this?

}