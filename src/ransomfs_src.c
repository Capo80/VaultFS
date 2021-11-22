#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "ransomfs.h"

/* Mount a ransomfs partition */
struct dentry *ransomfs_mount(struct file_system_type *fs_type,
                              int flags,
                              const char *dev_name,
                              void *data)
{
    struct dentry *dentry = mount_bdev(fs_type, flags, dev_name, data, ransomfs_fill_super);
    if (IS_ERR(dentry))
        printk(KERN_ERR "'%s' mount failure\n", dev_name);
    else
        printk(KERN_INFO"'%s' mount success\n", dev_name);

    return dentry;
}

/* Unmount a ransomfs partition */
void ransomfs_kill_sb(struct super_block *sb)
{

    kill_block_super(sb);

    printk(KERN_INFO"unmounted disk\n");
}

static struct file_system_type ransomfs_file_system_type = {
    .owner = THIS_MODULE,
    .name = "ransomfs",
    .mount = ransomfs_mount,
    .kill_sb = ransomfs_kill_sb,
    .fs_flags = FS_REQUIRES_DEV,
    .next = NULL,
};

static int __init ransomfs_init(void)
{
    int ret = ransomfs_init_inode_cache();
    if (ret) {
        printk(KERN_ERR "inode cache creation failed\n");
        return ret;
    }

    ret = register_filesystem(&ransomfs_file_system_type);
    if (ret) {
        printk(KERN_ERR "register_filesystem() failed\n");
        return ret;
    }

    printk(KERN_INFO "module loaded\n");
    return ret;
}

static void __exit ransomfs_exit(void)
{
    int ret = unregister_filesystem(&ransomfs_file_system_type);
    if (ret)
        printk(KERN_ERR "unregister_filesystem() failed\n");

    ransomfs_destroy_inode_cache();

    printk(KERN_INFO"module unloaded\n");
}

module_init(ransomfs_init);
module_exit(ransomfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Pasquale Caporaso");
MODULE_DESCRIPTION("A secure FileSystem");
