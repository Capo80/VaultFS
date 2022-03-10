#ifndef vaultfs_KPROBES_H
#define vaultfs_KPROBES_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/kprobes.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/sched.h>
#include <linux/version.h>

#include "../vaultfs.h"
#include "../vaultfs_security.h"

#define PATH_MAX 4096

// tell the return probe if we can let the operation finish
struct check_result {
	char pass;
};

struct fs_context_addr {
	struct fs_context* fc;
};

struct mount {
	struct hlist_node mnt_hash;
	struct mount *mnt_parent;
	struct dentry *mnt_mountpoint;
	struct vfsmount mnt;
    //..
};

int umount_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs);
int umount_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs);

#define KPROBES_COUNT 		1
#define KRETPROBES_COUNT 	2

extern struct kretprobe vaultfs_kretprobes[];
extern struct kprobe vaultfs_kprobes[];

#endif