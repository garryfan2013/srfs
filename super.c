#include <linux/fs.h>
#include <linux/list.h>
#include <linux/slab.h>
//#include <linux/stat.h>

#include "ksrfs.h"


extern void srfs_init_inode(struct inode *inode,
									struct inode *dir,
									umode_t mode);

extern int srfs_dir_add_entry(struct inode *dir,
										char *name,
										struct inode *ino);

static struct inode *srfs_alloc_inode(struct super_block *sb);

static void srfs_destroy_inode(struct inode *inode);


const struct super_operations srfs_sb_ops = {
	.alloc_inode = srfs_alloc_inode,
	.destroy_inode = srfs_destroy_inode,
};

static int srfs_group_init(struct srfs_group_info *gi, uint64_t index)
{
	struct srfs_inode_info *si;
	struct srfs_block_info *bi;
	char *blk_addr;
	uint64_t alloc_size;
	uint64_t i;

	gi->id = index;
	gi->ino_cnt = SRFS_GROUP_INODE_NR;
	gi->blk_cnt = SRFS_GROUP_DATA_BLOCK_NR;
	gi->blk_size = SRFS_BLOCK_SIZE;
	INIT_LIST_HEAD(&gi->ino_free);
	INIT_LIST_HEAD(&gi->blk_free);

	alloc_size = gi->ino_cnt*sizeof(struct srfs_inode_info) + 
					gi->blk_cnt*sizeof(struct srfs_block_info) +
					gi->blk_cnt*gi->blk_size;
	gi->store = (char *)kzalloc(alloc_size, GFP_KERNEL);
	if (!gi->store) {
		return -ENOMEM;
	}

	printk(KERN_INFO "alloc group store size = %llu, addr = %p\n", alloc_size, gi->store);

	for (i = 0; i < gi->ino_cnt; i++) {
		si = (struct srfs_inode_info *)gi->store + i;
		si->id = GENERATE_ID(index, i);
		list_add_tail(&si->list, &gi->ino_free);
	}

	bi = (struct srfs_block_info *)(gi->store + gi->ino_cnt*sizeof(struct srfs_inode_info));
	blk_addr = (char *)(bi + gi->blk_cnt);
	for (i = 0; i < gi->blk_cnt; i++) {
		bi->id = GENERATE_ID(index, i);
		bi->addr = blk_addr + i*gi->blk_size;
		list_add_tail(&bi->list, &gi->blk_free);

		printk(KERN_INFO "bi_addr: %p block[%llu]: %p\n", bi, bi->id, bi->addr);
		bi++;
	}

	return 0;
}

static void srfs_group_exit(struct srfs_group_info *gi) {
	if (gi->store) {
		kfree(gi->store);
	}
}

static int srfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct srfs_sb_info *sbi;
	struct inode *root;
	long ret = -EINVAL;
	int i = 0;

	if (data) {
		printk(KERN_INFO "srfs_fill_super %s\n", (char *)data);
	}
	
	ret = -ENOMEM;
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi) {
		goto failed;
	}

	sbi->group_cnt = SRFS_GROUP_NR;
	sbi->groups = kzalloc(SRFS_GROUP_NR*sizeof(struct srfs_group_info), GFP_KERNEL);
	if (!sbi->groups) {
		goto failed;
	}

	for (; i < sbi->group_cnt; i++) {
		ret = srfs_group_init(sbi->groups + i, i);
		if (ret < 0) {
			goto failed;
		}
	}

	sb->s_magic = SRFS_SUPER_MAGIC;
	sb->s_fs_info = sbi;
	sb->s_op = &srfs_sb_ops;

	/* Init root inode */
	ret = -ENOMEM;
	root = new_inode(sb);
	if (!root) {
		goto failed;
	}

	srfs_init_inode(root, NULL, S_IFDIR | S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		goto failed;
	}

	ret = srfs_dir_add_entry(root, ".", root);
	if (ret) {
		printk(KERN_ERR "srfs_add_entry %s failed: %ld\n", ".", ret);
		return ret;
	}
		
	ret = srfs_dir_add_entry(root, "..", root);
	if (ret) {
		printk(KERN_ERR "srfs_add_entry %s failed: %ld\n", "..", ret);
		return ret;
	}
	
	return 0;

failed:
	printk(KERN_ERR "srfs_fill_super failed: %ld\n", ret);
	if (sbi) {
		if (sbi->groups) {
			for (i = 0; i < sbi->group_cnt; i++) {
				srfs_group_exit(sbi->groups + i);
			}

			kfree(sbi->groups);
		}

		kfree(sbi);
	}

	return ret;
}

void srfs_kill_sb(struct super_block *sb)
{
	struct srfs_sb_info *sbi;
	int i = 0;

	printk(KERN_INFO "srfs_kill_sb\n");
	sbi = SRFS_SB(sb);
	for (; i < sbi->group_cnt; i++) {
		srfs_group_exit(sbi->groups + i);
	}

	if (sbi->groups) {
		kfree(sbi->groups);
	}

	kfree(sbi);
}

struct dentry* srfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, srfs_fill_super);
}

static struct inode *srfs_alloc_inode(struct super_block *sb)
{
	struct srfs_sb_info *sbi;
	struct srfs_inode_info *si;
	struct srfs_group_info *gi;

	sbi = SRFS_SB(sb);
	sbi->last_group = (++sbi->last_group == SRFS_GROUP_NR) ? 0 : sbi->last_group;
	gi = &(sbi->groups[sbi->last_group]);

	if (list_empty(&gi->ino_free)) {
		printk(KERN_WARNING "srfs allocate inode failed: inode resource exausted!\n");
		return NULL;
	}

	si = list_first_entry(&gi->ino_free, 
							struct srfs_inode_info, 
							list);
	list_del(&si->list);
	si->size = 0;
	INIT_LIST_HEAD(&si->data);

	/*
	 * The vfs inode is part of the srfs_inode_info, so its memory allocation is the responsibility of 
	 * srfs_inode_info allocator. 
	 * inode_init_once is invoked normally by kmem_cache_allocator, 
	 * but in this module kmem_cache_allocator is replaced by fixed size memory,
	 * therefore, it's necessary to invoke this init once procedure here for alternative.
	*/
	inode_init_once(&si->vfs_inode);

	return &si->vfs_inode;
}

/*
static void srfs_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	struct srfs_inode_info *si = SRFS_INDOE(inode);

	struct srfs_group_info *gi = GET_GROUP_FROM_VFS_INODE(inode);
	list_add_tail(&si->list, &gi->ino_free);
}
*/

static void srfs_destroy_inode(struct inode *inode)
{
	//call_rcu(&inode->i_rcu, srfs_i_callback);
}

struct srfs_block_info *srfs_alloc_block(struct super_block *sb, struct inode *inode)
{
	struct srfs_group_info *gi;
	struct srfs_block_info *bi;
	struct srfs_inode_info *si;

	gi = GET_GROUP_BY_INODE_ID(sb, inode->i_ino);
	if (unlikely(!gi)) {
		printk(KERN_ERR "Can't find group info for this inode: %ld\n", (long)inode->i_ino);
		return NULL;
	}
	
	if (list_empty(&gi->blk_free)) {
		printk(KERN_WARNING "srfs allocate block failed: block exausted!\n");
		return NULL;
	}

	printk(KERN_INFO "ready to allocate\n");

	si = SRFS_INODE(inode);
	bi = list_first_entry(&gi->blk_free,
							struct srfs_block_info,
							list);
	list_del(&bi->list);
	list_add_tail(&bi->list, &si->data);
	si->blk_cnt++;

	return bi;
}


