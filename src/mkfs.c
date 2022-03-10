#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/fs.h>
#include <unistd.h>
#include <openssl/sha.h>

#include "vaultfs.h"

struct superblock {
    struct vaultfs_superblock info;
    char padding[VAULTFS_BLOCK_SIZE - sizeof(struct vaultfs_superblock)]; /* Padding to match block size */
};

void zero_device(int fd, struct stat *fstats) {

    static const char zeros[4096];
    off_t size = fstats->st_size;
    lseek(fd, 0, SEEK_SET);
    int curr_blocks = 0, c = 0;
    int total_blocks = fstats->st_size/sizeof(zeros);
    while (size > sizeof(zeros)) {
        size -= write(fd, zeros, sizeof zeros);
        curr_blocks += 1;
        if (curr_blocks > c*(total_blocks/100)) {
            printf("Zeroing device... (%d%%)\n", c);
            c++;
        }

    }
    lseek(fd, 0, SEEK_SET);
    
}

static struct superblock *write_superblock(int fd, struct stat *fstats, unsigned char* password)
{
    struct superblock *sb = malloc(sizeof(struct superblock));
    if (!sb)
        return NULL;

    uint32_t blocks_count = fstats->st_size / VAULTFS_BLOCK_SIZE;
    uint32_t groups_count = (blocks_count-2) / VAULTFS_BLOCKS_PER_GROUP;
    uint32_t real_block_count = groups_count*(VAULTFS_BLOCKS_PER_GROUP - VAULTFS_INODES_GROUP_BLOCK_COUNT - 2);
    uint32_t inodes_count = groups_count*VAULTFS_INODES_PER_GROUP;

    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512(password, strlen((char*)password), hash);

    memset(sb, 0, sizeof(struct superblock));
    sb->info = (struct vaultfs_superblock) {
        .magic = htole32(VAULTFS_MAGIC),
        .blocks_count = htole32(real_block_count),
        .inodes_count = htole32(inodes_count),
        .free_blocks_count = htole32(real_block_count - 1),
        .free_inodes_count = htole32(inodes_count - 1),
        //.passwd_hash = sha512(password)   
    };

    memcpy(sb->info.passwd_hash, hash, SHA512_DIGEST_LENGTH);
    printf(
        "Superblock: (%lu)\n"
        "\tmagic=%#x\n"
        "\tnr_blocks=%u\n"
        "\tnr_inodes=%u"
        "\tnr_free_inodes=%u\n"
        "\tnr_free_blocks=%u\n",
        sizeof(struct superblock), sb->info.magic, sb->info.blocks_count,
        sb->info.inodes_count, sb->info.free_inodes_count,
        sb->info.free_blocks_count);


    int ret = write(fd, sb, sizeof(struct superblock));
    if (ret != sizeof(struct superblock)) {
        free(sb);
        return NULL;
    }

    return sb;
}

int write_root_inode(int fd) {

	struct vaultfs_inode root_inode;
    memset(&root_inode, 0, sizeof(struct vaultfs_inode));

    //init empty file root extent
    //this extend has an head and a leaf
    struct vaultfs_extent leaf_extent;
    memset(&leaf_extent, 0, sizeof(struct vaultfs_extent));
    leaf_extent.file_block = 0;
    leaf_extent.len = htole16(1);
    leaf_extent.data_block = htole32(2 + 2 + VAULTFS_INODES_GROUP_BLOCK_COUNT); //first datablock of the file system

    struct vaultfs_extent_header root_extent;
    memset(&root_extent, 0, sizeof(struct vaultfs_extent_header));
    root_extent.magic = VAULTFS_EXTENT_MAGIC;
    root_extent.entries = htole16(1);
    root_extent.max = VAULTFS_EXTENT_PER_INODE-1;
    root_extent.depth = 0;

	root_inode.i_mode = htole16(S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH);
	root_inode.i_uid = root_inode.i_gid = 0; //TODO change to current user? maybe not
	root_inode.i_size = htole32(VAULTFS_BLOCK_SIZE); //empty directory still has one block allocated
	root_inode.i_ctime = root_inode.i_atime = root_inode.i_mtime = htole32(0); //TODO change to current time
	root_inode.i_blocks = htole32(1);
    memcpy(root_inode.extent_tree, &root_extent, sizeof(struct vaultfs_extent_header));
    memcpy(root_inode.extent_tree + 1, &leaf_extent, sizeof(struct vaultfs_extent_header));

	int ret = write(fd, (char*) &root_inode, sizeof(struct vaultfs_inode));
    if (ret != sizeof(struct vaultfs_inode)) {
        return 0;
    }

    return ret;

}

int write_group_desc_table(int fd, uint32_t free_blocks, uint32_t free_inodes) {

    struct vaultfs_group_desc gdt;
    memset(&gdt, 0, sizeof(struct vaultfs_group_desc));

    gdt.free_blocks_count = free_blocks;
    gdt.free_inodes_count = free_inodes;

    int ret = write(fd, (char*) &gdt, sizeof(struct vaultfs_group_desc));
    if (ret != sizeof(struct vaultfs_group_desc)) {
        return 0;
    }

    return ret;

}

int write_first_bit_bitmap(int fd) {

    short first_word = 0x0001;
    int ret = write(fd, &first_word, sizeof(short));
    if (ret != sizeof(short)) { 
        return 0;
    }
    char* zeroes = malloc(VAULTFS_BLOCK_SIZE - sizeof(short));
	memset(zeroes, 0, VAULTFS_BLOCK_SIZE - sizeof(short));
	ret = write(fd, zeroes, VAULTFS_BLOCK_SIZE - sizeof(short));
	free(zeroes);
	
	if (ret != VAULTFS_BLOCK_SIZE - sizeof(short))
		return 0;
	return ret;

}

int write_padding(int fd, size_t padding_size) {

	char* zeroes = malloc(padding_size);
	memset(zeroes, 0, padding_size);
	int ret = write(fd, zeroes, padding_size);
	free(zeroes);
	
	if (ret != padding_size)
		return 0;
	return ret;
}

int main(int argc, char **argv) {
	
    if (argc != 3) {
        fprintf(stderr, "Usage: %s disk mount_password\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Open disk image */
    int fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("open():");
        return EXIT_FAILURE;
    }

    /* Get image size */
    struct stat stat_buf;
    int ret = fstat(fd, &stat_buf);
    if (ret) {
        perror("fstat():");
        ret = EXIT_FAILURE;
        goto fclose;
    }

    /* Get block device size */
    if ((stat_buf.st_mode & S_IFMT) == S_IFBLK) {
        long int blk_size = 0;
        ret = ioctl(fd, BLKGETSIZE64, &blk_size);
        if (ret != 0) {
            perror("BLKGETSIZE64:");
            ret = EXIT_FAILURE;
            goto fclose;
        }
        stat_buf.st_size = blk_size;
    }

    /* Check if image is large enough */
    long int min_size = 100 * VAULTFS_BLOCK_SIZE;
    if (stat_buf.st_size <= min_size) {
        fprintf(stderr, "File is not large enough (size=%ld, min size=%ld)\n",
                stat_buf.st_size, min_size);
        ret = EXIT_FAILURE;
        goto fclose;
    }

    //zeri device
    zero_device(fd, &stat_buf);
    printf("Device zeroed\n");

    /* Write superblock (block 0) */
    struct superblock *sb = write_superblock(fd, &stat_buf, (unsigned char*) argv[2]);
    if (!sb) {
        perror("write_superblock():");
        ret = EXIT_FAILURE;
        goto fclose;
    }

    //Writing group 0 table - one block/inode occupied by root
    if (!write_group_desc_table(fd, VAULTFS_BLOCKS_PER_GROUP-VAULTFS_INODES_GROUP_BLOCK_COUNT-3, VAULTFS_INODES_PER_GROUP-1)) {
		perror("group 0 desc table:");
    	ret =  EXIT_FAILURE;
		goto free_sb;
	}

    //write other tables
    uint32_t blocks_count = stat_buf.st_size / VAULTFS_BLOCK_SIZE;
    uint32_t block_desc_count = blocks_count / VAULTFS_BLOCKS_PER_GROUP;
    for (int i = 0; i < block_desc_count; i++) {
        if (!write_group_desc_table(fd, VAULTFS_BLOCKS_PER_GROUP-VAULTFS_INODES_GROUP_BLOCK_COUNT-2, VAULTFS_INODES_PER_GROUP)) {
            perror("groups desc table:");
            ret =  EXIT_FAILURE;
            goto free_sb;
        }  
    }

    //padding until the end of the block
    if (!write_padding(fd, VAULTFS_BLOCK_SIZE - (block_desc_count+1)*sizeof(struct vaultfs_group_desc))) {
    	perror("group desc write_padding():");
    	ret =  EXIT_FAILURE;
		goto free_sb;
	}

    printf("Written descriptor table block\n");

    //write group 0. it is different beacuse the root inode is alredy in there
	if (!write_first_bit_bitmap(fd)) {
		perror("bitmaps write_padding():");
    	ret =  EXIT_FAILURE;
		goto free_sb;
	}

	if (!write_first_bit_bitmap(fd)) {
		perror("bitmaps write_padding():");
    	ret =  EXIT_FAILURE;
		goto free_sb;
	}

    printf("Written group 0 bitmaps\n");

    //Write root inode
	if (!write_root_inode(fd)) {
		perror("root inode:");
		ret =  EXIT_FAILURE;
		goto free_sb;
	}

    printf("Written root inode\n");

    printf("Filesystem correctly formatted\n");

free_sb:
    free(sb);
fclose:
    close(fd);
    return ret;    
}