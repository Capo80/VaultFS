# FS Protection - Implementation Details

This Module implements 3 protective measures that, when active, have the following effects:
1. Write protection on file, files should only be written once and never modified;
2. Umount protection, the filesystem should be detached only on system shutdown;
3. Underlaying block device protection, the user should not be able to interact with the underlying block device after the system is mounted;

Protection 2 can be activated/deactivated with syscalls by using a single unique, secure password.

All protections are active by default at Filesystem Mounting. 

## Password persistance

There are 2 possible solutions to maintain the password:
- set up the password before mounting with a syscall and have it only in kernel memory;
- keep the password on the filesystem and load it in memory up automatically when mounting; 

The first option would allow us to control the password more easily at runtime, while the second would allow us to have a persistent password and a more automatic loading process.

In case of option 2, the best place to save the password would be the SuperBlock and to have the password be permanent while the FS is mounted.

We extract the password from the superblock by using the data paramenter of the mount syscall in the "ransomfs_mount" function, it could also be possible to change the password at this point if the user passes it as a parameter.

## Umount protection

We pair the mount point with the password during in the "ransomfs_mount" function.

When an umount is called we intercept it in the "security_sb_umount" function with a kprobe and block it if the mount point is the one we saved.

We don't need to worry about symlinks or mount namespaces, at this point thoose have already been translated by the kernel.

## Block Device protection

To write on the Block Device the FileSystem keeps a block_device structure inside his superblock and calls the block device operations directly using the buffer_head API to implement async operations and caching, this means we cannot destroy the linking of the operations or the FileSystem will not be able to interact with the device.

I believe tough that the only way for a user space process to be able to interact directly with the block device is with the open syscall, beacuse of this we can use a probe on the "security_file_open", check if the path opened is a block device and block the open if it is the one that the FileSystem is using.

We can pair the device with the password again in the "ransomfs_mount" function.

To recognize the block device we should be able to use the Major/Minor number (i think?).

## Write protection

Done entirely in the FS code, in the implementation of the "open" operation, we keep additional information in the inode to check if it has already been written and deny the open in WRITE MODE if it is.








