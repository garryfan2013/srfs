#ifndef __SRFS_H__
#define __SRFS_H__

#include <linux/kernel.h>
#include <linux/fs.h>

#define SRFS_SUPER_MAGIC 0x20160622

#define SRFS_GROUP_NR 1
#define SRFS_GROUP_DATA_BLOCK_NR 16
#define SRFS_GROUP_INODE_NR 128
#define SRFS_BLOCK_SIZE 1024

#define GROUP_NR_OFFSET 32

/* The first inode id should be 0 which is illeagal, a little trick to revise it */
#define INODE_ID_BASE 0x100

/* Calculate which group the onwer with id belongs to */
#define GET_GROUP_INDEX(id) ((id - INODE_ID_BASE) >> GROUP_NR_OFFSET)

/* OBJ could be either srfs_inode_info or srfs_block_info */
#define GET_OBJ_INDEX(id) ((id - INODE_ID_BASE ) & ((1UL << GROUP_NR_OFFSET) - 1))

/* Calculate id with group index and obj index */
#define GENERATE_ID(grp_idx, obj_idx) (((grp_idx << GROUP_NR_OFFSET) | obj_idx) + INODE_ID_BASE)

#define DIR_ENTRY_MAX_SIZE SRFS_BLOCK_SIZE

struct srfs_group_info {
	uint64_t id;

	uint64_t ino_cnt;

	uint64_t blk_cnt;

	uint64_t blk_size;
	
	/* point to the index of next free inode */
	struct list_head ino_free;

	/* point to the index of next free block */
	struct list_head blk_free;

	char *store;
};

struct srfs_sb_info {
	uint64_t version;
	uint64_t magic;

	uint8_t group_cnt;

	/* record the last time inode allocation group */
	uint8_t last_group;

	struct srfs_group_info *groups;
};

struct srfs_inode_info {
	uint64_t id;

	/* list head of data block list */
	struct list_head data;

	/* Inserted into ino_free field of srfs_group_info */
	struct list_head list;

	/* Actually written bytes */
	uint64_t size;

	/* Block count */
	uint64_t blk_cnt;

	/* vfs indeo part */
	struct inode vfs_inode;
};

struct srfs_block_info {
	uint64_t id;

	char *addr;
	/* 
	 * inked to data filed of srfs_inode_info after allocated
	 * or blk_free of srfs_group_info otherwise
	 */
	struct list_head list;
};

/*
 * Directory entry related definition
 */
typedef struct dir_entry_head
{
	uint64_t ino;
	uint64_t length;
}dir_entry_head_t;

static inline struct srfs_inode_info *SRFS_INODE(struct inode *inode)
{
	return container_of(inode, struct srfs_inode_info, vfs_inode);
}

static inline struct srfs_sb_info *SRFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct srfs_group_info *GET_GROUP_BY_INODE_ID(
	struct super_block *sb, uint64_t ino) {	
	return &(SRFS_SB(sb)->groups[GET_GROUP_INDEX(ino)]);
}

static inline struct srfs_inode_info *GET_SRFS_INODE_BY_ID(
	struct super_block *sb, uint64_t ino)
{
	return ((struct srfs_inode_info *)
			(SRFS_SB(sb)->groups[GET_GROUP_INDEX(ino)].store)
			+ GET_OBJ_INDEX(ino));
}

#endif