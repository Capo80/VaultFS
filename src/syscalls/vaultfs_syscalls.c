#include "vaultfs_syscalls.h"

#include "lib/usctm.h"

// ########## helpers ########################

unsigned long cr0;

inline void write_cr0_forced(unsigned long val)
{
    unsigned long __force_order;

    /* __asm__ __volatile__( */
    asm volatile(
        "mov %0, %%cr0"
        : "+r"(val), "+m"(__force_order));
}

inline void protect_memory(void)
{
    write_cr0_forced(cr0);
}

inline void unprotect_memory(void)
{
    write_cr0_forced(cr0 & ~X86_CR0_WP);
}

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
    struct sdesc *sdesc;
    int size;

    size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
    sdesc = kmalloc(size, GFP_KERNEL);
    if (!sdesc)
        return ERR_PTR(-ENOMEM);
    sdesc->shash.tfm = alg;
    return sdesc;
}

static int calc_hash(struct crypto_shash *alg,
             const unsigned char *data, unsigned int datalen,
             unsigned char *digest)
{
    struct sdesc *sdesc;
    int ret;

    sdesc = init_sdesc(alg);
    if (IS_ERR(sdesc)) {
        pr_info("can't alloc sdesc\n");
        return PTR_ERR(sdesc);
    }

    ret = crypto_shash_digest(&sdesc->shash, data, datalen, digest);
    kfree(sdesc);
    return ret;
}

static int calc_hash_sha512(const unsigned char *data, unsigned int datalen,
             unsigned char *digest)
{
    struct crypto_shash *alg;
    char *hash_alg_name = "sha512";
    int ret;

    alg = crypto_alloc_shash(hash_alg_name, 0, 0);
    if (IS_ERR(alg)) {
            pr_info("can't alloc alg %s\n", hash_alg_name);
            return PTR_ERR(alg);
    }
    ret = calc_hash(alg, data, datalen, digest);
    crypto_free_shash(alg);
    return ret;
}

// ############ syscalls #########################

//syscall to control the umount protection
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _umount_ctl, umount_security_info_t* , info, int, command){
#else
asmlinkage long sys_umount_ctl(umount_security_info_t* info, int command){
#endif

    struct vaultfs_security_info* cur;
    int ret = 0, lock_value;
    unsigned char* digest = kzalloc(VAULTFS_PASSWORD_SIZE, GFP_KERNEL);
    umount_security_info_t* k_info = kzalloc(sizeof(umount_security_info_t), GFP_KERNEL);

    AUDIT(TRACE)
    printk(KERN_INFO"Umount ctl called, command: %d\n", command);

    //compute sha of password
    __copy_from_user(k_info, info, sizeof(umount_security_info_t));

    if(calc_hash_sha512(k_info->password, strlen(k_info->password), digest) < 0) {
        AUDIT(TRACE)
        printk(KERN_INFO "Cannot calculate hash for %s\n", k_info->password);
        ret = -EIO;
        goto exit; 
    }

    AUDIT(TRACE)
    printk(KERN_INFO"Hash of password is: %s\n", digest);

    //handle lock/unlock
    switch (command)
    {
    case UMOUNT_LOCK:
        lock_value = 1;
        break;
    case UMOUNT_UNLOCK:
        lock_value = 0;
        break;
    default:                    
        AUDIT(ERROR)
        printk(KERN_INFO "Invalid command\n");
        ret = -EINVAL;
        goto exit;
    }

    //update list if we find a mount
    //TODO - do i need to do some locking here? i think not
    list_for_each_entry(cur, &security_info_list, node) {
        if (strcmp(cur->mount_path, k_info->mount_point) == 0) {
            if (memcmp(cur->password_hash, digest, VAULTFS_PASSWORD_SIZE) == 0) {
                AUDIT(TRACE)
                printk(KERN_INFO "Password is correct, umount lock changed to %d\n", lock_value);
                cur->umount_lock = lock_value;
                ret = 0;
            } else {
                AUDIT(TRACE)
                printk(KERN_INFO "Mount has been found, but the password is incorrect\n");
                ret = -EINVAL;
            }
            break;
        }
    }

exit:
    kfree(k_info);
    return ret;
}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
static unsigned long sys_umount_ctl = (unsigned long) __x64_sys_umount_ctl;	
#else
#endif


int insert_syscalls() {

    //find syscall table
	syscall_table_finder();

	if(!hacked_syscall_tbl){
		AUDIT(ERROR)
        printk(KERN_ERR"failed to find the sys_call_table\n");
		return -1;
	}

    //add new syscall
	cr0 = read_cr0();
    unprotect_memory();
    hacked_syscall_tbl[FIRST_NI_SYSCALL] = (unsigned long*) sys_umount_ctl;
    protect_memory();
    
	AUDIT(TRACE)
    printk(KERN_INFO"umount_ctl installed on the sys_call_table at displacement %d\n", FIRST_NI_SYSCALL);	

    return 0;

}


int remove_syscalls() {

    if(!hacked_syscall_tbl){
        AUDIT(TRACE)
        printk("failed to find the sys_call_table\n");
        return -1;
    }

    //put back sys_ni_syscall
    cr0 = read_cr0();
    unprotect_memory();
    hacked_syscall_tbl[FIRST_NI_SYSCALL] = (unsigned long*) hacked_syscall_tbl[SEVENTH_NI_SYSCALL];
    protect_memory();
    
    AUDIT(TRACE)
    printk(KERN_INFO"syscalls correctly unistalled\n"); 

    return 0;
    
}