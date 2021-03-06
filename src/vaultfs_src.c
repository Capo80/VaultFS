#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kprobes.h>

#include "vaultfs.h"
#include "vaultfs_security.h"
#include "kprobes/vaultfs_kprobes.h"
#include "syscalls/vaultfs_syscalls.h"
#include "device/vaultfs_controller.h"

// ################## file system ##############################

spinlock_t info_list_spinlock;
LIST_HEAD(security_info_list);
DEFINE_HASHTABLE(path_sb_hash, 2);

/* Mount a vaultfs partition */
struct dentry *vaultfs_mount(struct file_system_type *fs_type,
                              int flags,
                              const char *dev_name,
                              void *data)
{
    struct dentry* dentry;
    dev_t bdev;
    struct mount_sb_info* cur;
    struct vaultfs_security_info* new_node;
    unsigned long irq_flags;
    unsigned bkt;

    //TODO make passwd a parameter at mount?
    data = kzalloc(VAULTFS_PASSWORD_SIZE, GFP_KERNEL);

    dentry = mount_bdev(fs_type, flags, dev_name, data, vaultfs_fill_super);
    if (IS_ERR(dentry))  {
        AUDIT(ERROR)
        printk(KERN_ERR "'%s' mount failure\n", dev_name);
        return dentry;
    } else {
        AUDIT(TRACE)
        printk(KERN_INFO"'%s' mount success\n", dev_name);
    }

    //add information to security lists
    
    //bdev
    new_node = kzalloc(sizeof(struct vaultfs_security_info), GFP_KERNEL);
    bdev = name_to_dev_t(dev_name);
    if (!bdev) {
        AUDIT(ERROR)
        printk(KERN_ERR "Cannot setup security for: %s\n", dev_name);
        return dentry;
    } else {
        AUDIT(DEBUG)
        printk(KERN_INFO "FS block device is: (%d, %d) %s\n", MAJOR(bdev), MINOR(bdev), dev_name);
        new_node->bdev_id = bdev;
    }

    //password - maybe add a magic number to check for validity?
    AUDIT(DEBUG)
    printk("Found password hash in superblock: %s\n", (char*) data);
    memcpy(new_node->password_hash, data, VAULTFS_PASSWORD_SIZE);
    kfree(data);

    //mount point - FIXME add concurrency here - or maybe use data again?
	hash_for_each(path_sb_hash, bkt, cur, node) {
		AUDIT(DEBUG)
        printk("Mount point of FS is: %s\n", cur->mount_path);
        memcpy(new_node->mount_path, cur->mount_path, PATH_MAX);
        hash_del(&cur->node);
    }

    memcpy(new_node->bdev_path, dev_name, PATH_MAX);

    //protections are active by default
    new_node->bdev_lock = 1;
    new_node->umount_lock = 1;

    spin_lock_irqsave(&info_list_spinlock, irq_flags);
	list_add_rcu(&new_node->node, &security_info_list);
    spin_unlock_irqrestore(&info_list_spinlock, irq_flags);

    return dentry;
}

/* Unmount a vaultfs partition */
void vaultfs_kill_sb(struct super_block *sb)
{
    kill_block_super(sb);

    AUDIT(TRACE)
    printk(KERN_INFO"unmounted disk\n");
}

static struct file_system_type vaultfs_file_system_type = {
    .owner = THIS_MODULE,
    .name = "vaultfs",
    .mount = vaultfs_mount,
    .kill_sb = vaultfs_kill_sb,
    .fs_flags = FS_REQUIRES_DEV,
    .next = NULL,
};

static int __init vaultfs_init(void)
{
    int i, ret;

    hash_init(path_sb_hash);
	spin_lock_init(&info_list_spinlock);

    ret = vaultfs_init_inode_cache();
    if (ret) {
        AUDIT(ERROR)
        printk(KERN_ERR "inode cache creation failed\n");
        return ret;
    }

    ret = register_filesystem(&vaultfs_file_system_type);
    if (ret) {
        AUDIT(ERROR)
        printk(KERN_ERR "register_filesystem() failed\n");
        goto free_cache;
    }
    
    for (i = 0; i < KRETPROBES_COUNT; i++) {
        ret = register_kretprobe(&vaultfs_kretprobes[i]);
        if (ret < 0) {
            AUDIT(ERROR)
            printk(KERN_ERR "register_kretprobe %d failed, returned %d\n", i, ret);
            goto unreg_filesystem;
        }
    }

    for (i = 0; i < KPROBES_COUNT; i++) {
        ret = register_kprobe(&vaultfs_kprobes[i]);
        if (ret < 0) {
            AUDIT(ERROR)
            printk(KERN_ERR "register_kprobe failed, returned %d\n", ret);
            goto unreg_kretprobes;
        }
    }

    ret = register_controller();
    if (ret < 0) {
        AUDIT(ERROR)
        printk(KERN_ERR "Unable to register controller\n");
        goto unreg_kprobes;    
    }

    ret = insert_syscalls();
    if (ret) {
        AUDIT(ERROR)
        printk(KERN_ERR "syscall hacking failed\n");
        goto unreg_controller;
    }

    AUDIT(TRACE)
    printk(KERN_INFO "module loaded\n");
    return ret;

unreg_controller:
    unregister_controller();
unreg_kprobes:    
    for (i = 0; i < KPROBES_COUNT; i++)
        unregister_kprobe(&vaultfs_kprobes[i]);
unreg_kretprobes:
    for (i = 0; i < KRETPROBES_COUNT; i++)
        unregister_kretprobe(&vaultfs_kretprobes[i]);
unreg_filesystem:
    unregister_filesystem(&vaultfs_file_system_type);
free_cache:
    vaultfs_destroy_inode_cache();
    return ret;

}

static void __exit vaultfs_exit(void)
{   
    int i;
    int ret = unregister_filesystem(&vaultfs_file_system_type);
    if (ret)
        AUDIT(ERROR)
        printk(KERN_ERR "unregister_filesystem() failed\n");

    vaultfs_destroy_inode_cache();

    for (i = 0; i < KPROBES_COUNT; i++)
        unregister_kprobe(&vaultfs_kprobes[i]);

    for (i = 0; i < KRETPROBES_COUNT; i++)
        unregister_kretprobe(&vaultfs_kretprobes[i]);
	
    unregister_controller();

    ret = remove_syscalls();
    if (ret < 0)
        AUDIT(ERROR)
        printk(KERN_ERR "unable to remove syscalls\n");

    AUDIT(TRACE)
    printk(KERN_INFO"module unloaded\n");
}

module_init(vaultfs_init);
module_exit(vaultfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pasquale Caporaso");
MODULE_DESCRIPTION("A secure FileSystem");
