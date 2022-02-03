#include "ransomfs_controller.h"

//constants
#define MAX_COMMAND_SIZE        17
#define MAX_PROT_STRING_SIZE    8
#define COMMAND_SEPARATOR       ":"

//device data
dev_t device_major;
struct class *device_class;
struct cdev dev_data;

//sha512 struct
struct sdesc {
    struct shash_desc shash;
    char ctx[];
};

//controller commands
#define UMOUNT_LOCK         0x0
#define UMOUNT_UNLOCK       0x1
#define CHANGE_FILE_PROT    0x2

char commad_strings[][MAX_COMMAND_SIZE] = {
    "UMOUNT_LOCK",
    "UMOUNT_UNLOCK",
    "CHANGE_FILE_PROT",
};

/*
    Controller commands are in the form:
        COMMAND:<arg1>:<arg2> ....
    
    Implemented commands are:
        - UMOUNT_LOCK/UNLOCK:<mount_point>:<password>
        - CHANGE_FILE_PROT:<mount_point>:<password>:<new_prot_mode> (prot modes are: S_IFREG, S_IFFW or S_IFMS)
    
*/

// #################################### helpers #####################################

int parse_command(char* command) {

    int i;

    for (i = 0; i < ARRAY_SIZE(commad_strings); i++) {
        if (strcmp(command, commad_strings[i]) == 0) 
            return i;
    }

    return -1;
}

uint8_t parse_protection(char* protection) {

    if (strcmp(protection, "P_RG") == 0)
        return P_RG;
    else if (strcmp(protection, "P_MS") == 0)
        return P_MS;
    else if (strcmp(protection, "P_FW") == 0)    
        return P_FW;
    else
        return -1;

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

// ########################  commands implementation ##########################

int change_umount_lock(char* mount_point,  char* password, int command) {

    struct ransomfs_security_info* cur;
    int ret = 0, lock_value;
    unsigned char* digest = kzalloc(RANSOMFS_PASSWORD_SIZE, GFP_KERNEL);

    //compute sha of password
    if(calc_hash_sha512(password, strlen(password), digest) < 0) {
        AUDIT(TRACE)
        printk(KERN_INFO "Cannot calculate hash for %s\n", password);
        ret = -1;
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
            ret = -1;
            goto exit;
    }

    //update list if we find a mount
    //TODO - do i need to do some locking here? i think not
    list_for_each_entry(cur, &security_info_list, node) {
        if (strcmp(cur->mount_path, mount_point) == 0) {
            if (memcmp(cur->password_hash, digest, RANSOMFS_PASSWORD_SIZE) == 0) {
                AUDIT(TRACE)
                printk(KERN_INFO "Password is correct, umount lock changed to %d\n", lock_value);
                cur->umount_lock = lock_value;
                ret = 0;
                goto exit;
            } else {
                AUDIT(TRACE)
                printk(KERN_INFO "Mount has been found, but the password is incorrect\n");
                ret = -1;
                goto exit;
            }
            break;
        }
    }

exit:
    kfree(digest);
    return ret;
}

int change_file_protection(char* mount_point, char* password, char* new_protection) {

    struct ransomfs_security_info* cur;
    char* bdev_path;
    struct block_device* bdev;
    struct super_block* sb;
    struct ransomfs_sb_info* sb_info;
    unsigned short new_prot_level;
    int ret = 0;
    unsigned char* digest = kzalloc(RANSOMFS_PASSWORD_SIZE, GFP_KERNEL);

    //check if protection is valid
    new_prot_level = parse_protection(new_protection);
    if ((short) new_prot_level == -1) {
        AUDIT(TRACE)
        printk(KERN_INFO "invalid protection level %s\n", new_protection);
        ret = -1;
        goto exit;
    }

    //compute sha of password
    if(calc_hash_sha512(password, strlen(password), digest) < 0) {
        AUDIT(TRACE)
        printk(KERN_INFO "Cannot calculate hash for %s\n", password);
        ret = -1;
        goto exit;
    }

    AUDIT(TRACE)
    printk(KERN_INFO"Hash of password is: %s\n", digest);

    //go throgh list to check password and get block device
    list_for_each_entry(cur, &security_info_list, node) {
        if (strcmp(cur->mount_path, mount_point) == 0) {
            if (memcmp(cur->password_hash, digest, RANSOMFS_PASSWORD_SIZE) == 0) {
                bdev_path = cur->bdev_path;
                AUDIT(TRACE)
                printk(KERN_INFO "Password is correct, bdev path is %s\n", bdev_path);
                break;
            } else {
                AUDIT(TRACE)
                printk(KERN_INFO "Mount has been found, but the password is incorrect\n");
                ret = -1;
                goto exit;
            }
            break;
        }
    }

    //now get the superblock
    bdev = blkdev_get_by_path(bdev_path, FMODE_READ, NULL);
    if (!IS_ERR(bdev)) {
        sb = get_super(bdev);
        if (sb != NULL) {

            sb_info = RANSOMFS_SB(sb);
            //change protection
            sb_info->file_prot_mode = new_prot_level;
            drop_super(sb);
            ret = 0;
        } else {
            AUDIT(ERROR)
            printk(KERN_ERR "Cannot access supervlock\n");
            ret = -1;
            goto exit;
        }
        blkdev_put(bdev, FMODE_READ);
    } else {
        AUDIT(ERROR)
        printk(KERN_ERR "Cannot access block dev %s\n", bdev_path);
        ret = -1;
        goto exit;
    }

    AUDIT(TRACE)
    printk(KERN_INFO "Protection level changed to %s", new_protection);

exit:
    kfree(digest);
    return ret;

}


// ############################### ######device operations ##########################

//TODO change this with a log with the result of user commands
ssize_t controller_read(struct file *filp, char *buff, size_t len, loff_t *off) {

    #define SIZE 6
    char message[SIZE] = "hello";

    AUDIT(TRACE)
    printk(KERN_INFO "dev read called\n");

    //check that offset is whithin boundaries
	if (*off >= SIZE)
		return 0;
	else if (*off + len > SIZE)
		len = SIZE - *off;

    if (copy_to_user(buff, message, len))
		return -EFAULT;
    
    *off += len;
    return len;

}

ssize_t controller_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

    char* command_string, *mount_point, *password, *protection;
    int command_number;
    char* orginal_buffer;
    char* user_command = kzalloc(len, GFP_KERNEL);
    orginal_buffer = user_command;

    __copy_from_user(user_command, buff, len);

    AUDIT(TRACE)
    printk(KERN_INFO "dev write called with: %s\n", user_command);

    command_string = strsep(&user_command, COMMAND_SEPARATOR);

    command_number = parse_command(command_string);
    switch (command_number) {
        case UMOUNT_LOCK:
        case UMOUNT_UNLOCK:
            mount_point = strsep(&user_command, COMMAND_SEPARATOR);
            password = strsep(&user_command, COMMAND_SEPARATOR);
            AUDIT(DEBUG)
            printk(KERN_INFO "(%s, %s)\n", mount_point, password);
            if (change_umount_lock(mount_point, password, command_number) == 0) {
                AUDIT(TRACE)
                printk(KERN_INFO "Command %s successful\n", command_string);
            } else {
                AUDIT(TRACE)
                printk(KERN_INFO "Command %s failed\n", command_string);
            }
            break;
        case CHANGE_FILE_PROT:
            mount_point = strsep(&user_command, COMMAND_SEPARATOR);
            password = strsep(&user_command, COMMAND_SEPARATOR);
            protection = strsep(&user_command, COMMAND_SEPARATOR);
            AUDIT(DEBUG)
            printk(KERN_INFO "(%s, %s, %s)\n", mount_point, password, protection);
            if (change_file_protection(mount_point, password, protection) == 0) {
                AUDIT(TRACE)
                printk(KERN_INFO "Command %s successful\n", command_string);
            } else {
                AUDIT(TRACE)
                printk(KERN_INFO "Command %s failed\n", command_string);
            }
            break;
        default:
            AUDIT(TRACE)
            printk(KERN_INFO "Unknown command %s\n", command_string);
    }

    kfree(orginal_buffer);
    return len;
}

//device fops
struct file_operations fops = {
  .owner = THIS_MODULE,
  .read = controller_read, //TODO implement a log for the result of the commands
  .write = controller_write,
};


int register_controller() {

    if (alloc_chrdev_region(&device_major , 0, 1, DEVICE_NAME) < 0) {
        AUDIT(TRACE)
        printk("Region registering of device failed\n");
        return -1;
    }

    device_class = class_create(THIS_MODULE, DEVICE_CLASS);
    
    cdev_init(&dev_data, &fops);
    device_create(device_class, NULL, device_major, NULL, DEVICE_NAME);
    if (cdev_add(&dev_data, device_major, 1)){
        AUDIT(TRACE)
        printk(KERN_INFO "Failed to register new device\n");
        return -1;
    }

    AUDIT(TRACE)
    printk("Device registered, it is assigned major number %d\n", MAJOR(device_major));
    return 0;
}

void unregister_controller() {

    device_destroy(device_class, device_major);
    class_destroy(device_class);
    cdev_del(&dev_data);
    unregister_chrdev_region(device_major, 1);

    printk("Device unregistered, it was assigned major number %d\n", MAJOR(device_major));
}
