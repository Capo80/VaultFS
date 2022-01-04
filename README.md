# RansomFS
A simplified version of the ext4 FileSystem with added security features. This FileSystem is designed for back-ups of highly sensible data and is intended as a defensive measure against Ransomware attacks.

The security features that i plan to implement are:
- Write protection on file, files should only be written once and never modified;
- Umount protection, the filesystem should be detached only on system shutdown;
- Underlaying block device protection, the user should not be able to interact with the underlying block device after the system is mounted;

Details on the implementation are in the [implementation details file](docs/Implemention%20details%20of%20the%20protections.md).

## Disk-Layout

The disk layout is based on the ext4 FileSystem, the disk is divided in groups and each group has the following organization: 

| ext4 superblock <br/> (only in group 0) | Group descriptor table <br/> (only in group 0) | Data Block bitmap | Inode Bitamp | Inode Table | Data Blocks |
|:-:|:-:|:-:|:-:|:-:|:-:|
| 1 block | 1 block | 1 block | 1 block | 512 blocks | >= 1 Block |

The separation in blocks allows us to allocate and find blocks more efficiently and it also makes it esier to avoid fragmentation.

Sizes and limitations are the same of the ext4 FileSystem with a block size of 4 Kb.

The major differences with the ext4 FileSystem are:
- No Reserved GDT blocks, ext4 allocates blocks that can be used to resize the FileSystem if needed, this FileSystem will not have this feature;
- Linear addressing for directories, directory will be treated as linear arrays, not as hash trees;
- No group 0 padding, the ext4 FileSystem leaves 1024 bytes padding at the start of the for boot sectors and other operating system oddities, this FileSytem is not maent to house an OS so this is not needed;
- No journaling, definitely something that should be implemented, but i believe it is out of the scope of the project for now.
- Fixed block size of 4Kb;
- No 32-bit mode for addressing;

## Build

Enter the source folder and run: ```make```

The MakeFile will create a RansomFS formatted test image in the current folder name ```test.img```

Default size for the test image is 1GB, to change it edit the "IMAGESIZE" variable in the Makefile.

After this we can insert the module and mount the filesystem with the file on a virtual block device with:

```
insmod ransomfs.ko
mount -o loop -t ransomfs test.img <directory>
```

## Test

No tests have been implemented so far.

## Current Progress

| State | Task | Notes |
|:-:|:-:|:-:|
| :heavy_check_mark: | FileSystem registration | |
| :heavy_check_mark: | Mounting and Unmounting | |
| :heavy_check_mark: | Directory Read | |
| :heavy_check_mark: | File/Directory Creation | |
| :heavy_check_mark: | File Write | |
| :heavy_check_mark: | File Read | |
| :gear: | Write Protection | |
| :heavy_check_mark: | Umount Protection | |
| :heavy_check_mark: | Block Device Protection | |

## TODOs

| State | Task | Difficulty |
|:-:|:-|:-:|
|:x:| Create some sort of callback mechanism for the traversing of the extent tree | 5/5 |
|:x:| Implement concurrency management on the cached gdt | 1/5 |
