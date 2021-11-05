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

#include "ransomfs.h"

struct superblock {
    struct ransomfs_sb_info info;
    char padding[RANSOMFS_BLOCK_SIZE - sizeof(struct ransomfs_sb_info)]; /* Padding to match block size */
};

static struct superblock *write_superblock(int fd, struct stat *fstats)
{
    struct superblock *sb = malloc(sizeof(struct superblock));
    if (!sb)
        return NULL;

    uint64_t blocks_count = fstats->st_size / RANSOMFS_BLOCK_SIZE;
    uint64_t inodes_count = blocks_count; //TODO this is wrong
    uint64_t mod = inodes_count % RANSOMFS_INODES_PER_BLOCK;
    if (mod)
        inodes_count += RANSOMFS_INODES_PER_BLOCK - mod;

    memset(sb, 0, sizeof(struct superblock));
    sb->info = (struct ransomfs_sb_info) {
        .magic = htole32(RANSOMFS_MAGIC),
        .blocks_count = htole64(blocks_count),
        .inodes_count = htole64(inodes_count),
        .free_inodes_count = htole64(inodes_count - 1),
        .free_blocks_count = htole64(blocks_count - 1), //TODO this is wrong
    };

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

	struct ransomfs_inode root_inode;

    //init empty file root extent
    //this extend has an head and a leaf
    struct ransomfs_extent leaf_extent;
    leaf_extent.file_block = 0;
    leaf_extent.len = 1;
    leaf_extent.data_block = 516; //first datablock of the file system

    struct ransomfs_extent_header root_extent;
    root_extent.magic = RANSOMFS_EXTENT_MAGIC;
    root_extent.entries = 1;
    root_extent.max = RANSOMFS_EXTENT_PER_INODE;
    root_extent.depth = 0;

	root_inode.i_mode = htole16(S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH);
	root_inode.i_uid = root_inode.i_gid = 0; //TODO change to current user
	root_inode.i_size = RANSOMFS_BLOCK_SIZE; //empty directory still has one block allocated
	root_inode.i_ctime = root_inode.i_atime = root_inode.i_mtime = htole32(0); //TODO change to current time
	root_inode.i_blocks = htole32(1);
    memcpy(root_inode.extent_tree, &root_extent, sizeof(struct ransomfs_extent_header));
    memcpy(root_inode.extent_tree + 1, &leaf_extent, sizeof(struct ransomfs_extent_header));

	int ret = write(fd, (char*) &root_inode, sizeof(struct ransomfs_inode));
    if (ret != sizeof(struct ransomfs_inode)) {
        return 0;
    }

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
	
    if (argc != 2) {
        fprintf(stderr, "Usage: %s disk\n", argv[0]);
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
    long int min_size = 100 * RANSOMFS_BLOCK_SIZE;
    if (stat_buf.st_size <= min_size) {
        fprintf(stderr, "File is not large enough (size=%ld, min size=%ld)\n",
                stat_buf.st_size, min_size);
        ret = EXIT_FAILURE;
        goto fclose;
    }

    /* Write superblock (block 0) */
    struct superblock *sb = write_superblock(fd, &stat_buf);
    if (!sb) {
        perror("write_superblock():");
        ret = EXIT_FAILURE;
        goto fclose;
    }

    //TODO write initial descriptor table
    uint64_t blocks_count = stat_buf.st_size / RANSOMFS_BLOCK_SIZE;
    uint64_t block_desc_count = (blocks_count / RANSOMFS_BLOCKS_PER_GROUP + 1) / ( RANSOMFS_BLOCK_SIZE / sizeof(struct ransomfs_group_desc)) + 1;
    if (!write_padding(fd, RANSOMFS_BLOCK_SIZE*block_desc_count)) {
    	perror("group desc write_padding():");
    	ret =  EXIT_FAILURE;
		goto free_sb;
	}
    printf("Written %ld block of desc table\n", block_desc_count);

    //TODO write initial bitmaps (just flip first bit probrably)
	if (!write_padding(fd, RANSOMFS_BLOCK_SIZE*2)) {
		perror("bitmaps write_padding():");
    	ret =  EXIT_FAILURE;
		goto free_sb;
	}

    printf("Curr file position: %ld\n", lseek(fd, 0, SEEK_CUR));

    /* Write root inode */
	if (!write_root_inode(fd)) {
		perror("root inode write_padding():");
		ret =  EXIT_FAILURE;
		goto free_sb;
	}

free_sb:
    free(sb);
fclose:
    close(fd);
    return ret;    
}