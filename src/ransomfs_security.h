#ifndef RANSOMFS_SECURITY_H
#define RANSOMFS_SECURITY_H

#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include "ransomfs.h"

//element of the hash table to connect mount point and superblock information
struct mount_sb_info {
	char* mount_path;
	struct hlist_node node;
};
extern DECLARE_HASHTABLE(path_sb_hash, 2); //the info here is really short lived, we don't need that many buckets

struct ransomfs_security_info {

	dev_t bdev_id;											//id of the block device
	char bdev_path[PATH_MAX];								//path of the block device
	char mount_path[PATH_MAX];								//mount point of the file system
	char bdev_lock;											//lock flag for the bdev   (1 = locked, 0 = free)
	char umount_lock;										//lock flag for the umount (1 = locked, 0 = free)
	unsigned char password_hash[RANSOMFS_PASSWORD_SIZE];	//hash of the filesystem password
	
	struct list_head node;
	struct rcu_head rcu;
};
extern struct list_head security_info_list;
extern spinlock_t info_list_spinlock;

#endif