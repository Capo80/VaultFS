#include "vaultfs_kprobes.h"

// ##################### umount protection ###############################

static void sucurity_info_reclaim_callback(struct rcu_head *rcu) {
	struct vaultfs_security_info *info = container_of(rcu, struct vaultfs_security_info, rcu);
	if (info != NULL)	
		kfree(info);
}

//mounted on security_sb_umount entry
int umount_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct check_result *res = (struct check_result *)ri->data;
	struct vaultfs_security_info* info;
    
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
	struct vfsmount* mount_point = (struct vfsmount*) regs_get_register(regs, offsetof(struct pt_regs, di)); //first argument
#else
	struct vfsmount* mount_point = (struct vfsmount*) regs_get_kernel_argument(regs, 0);
#endif

	char* buffer = kzalloc(4096, GFP_KERNEL);
	char* path;
	struct mount* real_mount = container_of(mount_point, struct mount, mnt);
	path = dentry_path_raw(real_mount->mnt_mountpoint, buffer, 4096);
	AUDIT(DEBUG)
	printk(KERN_INFO "umount called: %s\n", path);

	//pass by deafult
	res->pass = 1;

	AUDIT(DEBUG)
	printk(KERN_INFO "umount called: %s - %d - %d - %d \n", path, current->tgid, current->parent->tgid, current->real_parent->tgid);

	//let systemd/init umount
	if (current->tgid == 1 || current->parent->tgid == 1)
		return 0;

	//check if path is protected
	rcu_read_lock();
	list_for_each_entry(info, &security_info_list, node) {
		AUDIT(DEBUG)
		printk(KERN_INFO "checking: %s\n", info->mount_path);	
		if (strcmp(info->mount_path, path) == 0) {	
			//is is protected - is the lock active?
			if (info->umount_lock) {
				AUDIT(TRACE)
				printk(KERN_INFO "Blocked umount on protected path\n");
				//it is active - fail
				res->pass = 0;
			} else {
				//it is not - clean up the list - pass
				spin_lock(&info_list_spinlock); // kprobe is unpreemptable
				list_del_rcu(&info->node);
				spin_unlock(&info_list_spinlock);
				call_rcu(&info->rcu, sucurity_info_reclaim_callback);
			}
			break;
		}
	}
	rcu_read_unlock();
	AUDIT(DEBUG)
	printk(KERN_INFO "umount exiting: %s\n", path);

	kfree(buffer);
	return 0;
}
NOKPROBE_SYMBOL(umount_entry_handler);

//mounted on security_sb_umount return
int umount_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct check_result *res = (struct check_result *)ri->data;
	if (!res->pass) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
		regs->ax = (unsigned long) -EPERM;
#else
		regs_set_return_value(regs, -EPERM);
#endif
	}
	return 0;
}
NOKPROBE_SYMBOL(umount_ret_handler);

//##################### block dev protection ###############################

//mounted on security_file_open entry
int open_entry_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct check_result *res = (struct check_result *)ri->data;
	struct vaultfs_security_info* info;
	dev_t bdev;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
	struct file* file = (struct file*) regs_get_register(regs, offsetof(struct pt_regs, di)); //first argument
#else
	struct file* file = (struct file*)  regs_get_kernel_argument(regs, 0);
#endif

    char* buffer = kzalloc(4096, GFP_KERNEL);
    char* path;
    path = d_path(&file->f_path, buffer, 4096);

	//pass by deafult
	res->pass = 1;
	bdev = name_to_dev_t(path);
	if (bdev) {
		AUDIT(TRACE)
		printk(KERN_INFO "Detected device opening: %s -> (%d, %d)\n", path, MAJOR(bdev), MINOR(bdev));		
		
		//check if device is protected
		rcu_read_lock();
		list_for_each_entry(info, &security_info_list, node) {
			AUDIT(DEBUG)
			printk(KERN_INFO "checking: %s (%d, %d)\n", info->mount_path, MAJOR(bdev), MINOR(bdev));		
			if (info->bdev_lock && info->bdev_id == bdev) {
				AUDIT(TRACE)
				printk(KERN_INFO "Denied access on protected block_device\n");
				//it is protected - fail
				res->pass = 0;
				break;
			}
		}
		rcu_read_unlock();
	}

	kfree(buffer);
	return 0;
}
NOKPROBE_SYMBOL(open_entry_handler);

//mounted on security_file_open return
int open_ret_handler(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct check_result *res = (struct check_result *)ri->data;
	if (!res->pass) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
		regs->ax = (unsigned long) -EPERM;
#else
		regs_set_return_value(regs, -EPERM);
#endif
	}
	return 0;
}
NOKPROBE_SYMBOL(open_ret_handler);

//attatched on security_sb_mount entry
int sb_mount_entry_handler(struct kprobe *ri, struct pt_regs *regs)
{	
	struct mount_sb_info* info;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,20,0)
	struct path* new_mount = (struct path*) regs_get_register(regs, offsetof(struct pt_regs, si)); //second argument
#else
	struct path* new_mount = (struct path*) regs_get_kernel_argument(regs, 1);
#endif

	
    char* buffer = kzalloc(4096, GFP_KERNEL);
    char* path;
    path = dentry_path_raw(new_mount->dentry, buffer, 4096);
	
	AUDIT(DEBUG)
	printk(KERN_INFO "mount called by %d: %s\n", current->pid, path);

	//read mount point - save in hash to associate with password
	info = kzalloc(sizeof(struct mount_sb_info), GFP_KERNEL);
	info->mount_path = path;
	hash_add(path_sb_hash, &info->node, current->pid);

	return 0;
}
NOKPROBE_SYMBOL(sb_mount_entry_handler);

struct kretprobe vaultfs_kretprobes[] = {
	{
		//umount control
		.handler		= umount_ret_handler,
		.entry_handler	= umount_entry_handler,
		.data_size		= sizeof(struct check_result),
		/* Probe up to 20 instances concurrently. */
		.maxactive		= 20,
		.kp = {
			.symbol_name = "security_sb_umount" 
		}
	},
	{
		//block device control
		.handler		= open_ret_handler,
		.entry_handler	= open_entry_handler,
		.data_size		= sizeof(struct check_result),
		/* Probe up to 20 instances concurrently. */
		.maxactive		= 20,
		.kp = {
			.symbol_name = "security_file_open" 
		}
	},
};

struct kprobe vaultfs_kprobes[] = {
	{
		//check mount of secure filesystem
		.pre_handler	= sb_mount_entry_handler,
		.symbol_name 	= "security_sb_mount" 
	},
};