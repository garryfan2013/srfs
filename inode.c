#include <linux/kernel.h>
#include <linux/fs.h>

#include "ksrfs.h"

struct srfs_block_info *srfs_alloc_block(struct super_block *sb,
												struct inode *inode);


static int srfs_create(struct inode *dir,
							struct dentry *dentry,
							umode_t mode, bool excl);


static int srfs_mkdir(struct inode *dir, 
							struct dentry *dentry,
							umode_t mode);

static struct dentry *srfs_lookup(struct inode *dir,
										struct dentry *dentry,
										unsigned int flags);

extern const struct file_operations srfs_file_ops;
extern const struct file_operations srfs_dir_ops;


const struct inode_operations srfs_inode_ops = {
	.create = srfs_create,
	.mkdir = srfs_mkdir,
	.lookup = srfs_lookup,
};


/*
 * Used for initialize speicific fs inode structure when mkdir or create a new file 
 */
void srfs_init_inode(struct inode *inode, struct inode *dir, umode_t mode)
{
	struct srfs_inode_info *si = SRFS_INODE(inode);

	inode->i_ino = si->id;
	inode->i_mode = mode;
	inode->i_op = &srfs_inode_ops;
	inode->i_atime = inode->i_mtime
					= inode->i_ctime
					= current_kernel_time();

	inode_init_owner(inode, dir, mode);

	if (S_ISDIR(mode)) {
		inode->i_fop = &srfs_dir_ops;
	} else if (S_ISREG(mode)) {
		inode->i_fop = &srfs_file_ops;
	} else {
		printk(KERN_WARNING "Can't assign file ops for inode %lu\n", inode->i_ino);
		inode->i_fop = NULL;
	}
}

/*
 * Used for initialize specific fs inode when lookup
 */
void srfs_fill_inode(struct inode *inode)
{
	atomic_set(&inode->i_count, 1);
	inode->__i_nlink = 1;
	inode->i_opflags = 0;
	atomic_set(&inode->i_writecount, 0);
	atomic_set(&inode->i_dio_count, 0);

#ifdef CONFIG_FSNOTIFY
	inode->i_fsnotify_mask = 0;
#endif

	//this_cpu_inc(nr_inodes);
}

int srfs_dir_add_entry(struct inode *dir,
										char *name,
										struct inode *ino)
{
	struct srfs_inode_info *parent_si;
	uint64_t ent_size;
	dir_entry_head_t *eh;
	char *ename;
	struct srfs_block_info *bi;

	if (!S_ISDIR(dir->i_mode)) {
		printk(KERN_ERR "Can't add entry for a non directory object\n");
		return -EINVAL;
	}

	parent_si = SRFS_INODE(dir);

	if (list_empty(&parent_si->data)) {
		bi = srfs_alloc_block(dir->i_sb ,dir);
		if (!bi) {
			return -ENOMEM;
		}
	}

	printk(KERN_INFO "srfs_adir_add_entry after alloc block\n");

	ent_size = sizeof(dir_entry_head_t) + strlen(name) + 1;
	if (parent_si->size + ent_size > DIR_ENTRY_MAX_SIZE) {
		printk(KERN_ERR "Not enough space for new direcotry entry %s\n", name);
		return -ENOSPC;
	}

	bi = list_first_entry(&parent_si->data , struct srfs_block_info, list);
	if (unlikely(!bi)) {
		printk(KERN_WARNING "no data block for this inode\n");
		return -ENOSPC;
	}
	
	eh = (dir_entry_head_t *)(bi->addr + parent_si->size);
	eh->ino = ino->i_ino;
	eh->length = strlen(name) + 1;
	ename = (char *)(eh + 1);
	strcpy(ename, name);
	parent_si->size += ent_size;

	return 0;
}

static struct dentry *srfs_dir_find_entry(struct inode *dir, 
											struct dentry *dentry, 
											unsigned int flags)
{
	struct srfs_inode_info *si;
	struct srfs_block_info *bi;
	dir_entry_head_t *eh;
	uint64_t offset = 0;
	char *ename;

	si = SRFS_INODE(dir);
	if (list_empty(&si->data)) {
		return NULL;
	}

	bi = list_first_entry(&si->data, struct srfs_block_info, list);
	while (si->size > offset) {
		eh = (dir_entry_head_t *)(bi->addr + offset);
		ename = (char *)(eh + 1); 
		if (0 == strcmp(ename, dentry->d_name.name)) {
			si = GET_SRFS_INODE_BY_ID(dir->i_sb, eh->ino);
			if (!si) {
				printk(KERN_ERR "Can't find requested inode by id %ld\n", (long)eh->ino);
				return ERR_PTR(-ENOENT);
			}

			srfs_fill_inode(&si->vfs_inode);
			d_add(dentry, &si->vfs_inode);
			return NULL;
		}
		offset += eh->length;
	}

	return NULL;
}

static int __srfs_create_inode(struct inode *dir,
										struct dentry *dentry,
										umode_t mode)
{
	struct super_block *sb;
	struct srfs_sb_info *sbi;
	struct inode *ino;
	int ret;

	sb = dir->i_sb;
	sbi = SRFS_SB(sb);

	ino = new_inode(sb);
	if (!ino) {
		ret = -ENOMEM;
		goto failed;
	}

	srfs_init_inode(ino, dir, mode);
	ret = srfs_dir_add_entry(dir, (char *)dentry->d_name.name, ino);
	if (ret != 0) {
		goto failed;
	}

	d_add(dentry, ino);

	return 0;

failed:
	return ret;
}

static int srfs_create(struct inode *dir,
							struct dentry *dentry,
							umode_t mode, bool excl)
{
	printk("%s <--\n", __func__);
	return __srfs_create_inode(dir, dentry, mode);
}

static int srfs_mkdir(struct inode *dir,
							struct dentry *dentry,
							umode_t mode)
{
	int ret;
	struct inode *inode;

	printk("%s <--\n", __func__);
	mode |= S_IFDIR;
	ret = __srfs_create_inode(dir, dentry, mode);
	if (ret) {
		printk(KERN_ERR "[srfs_create] __srfs_create_inode failed: %d\n", ret);
		return ret;
	}

	inode = dentry->d_inode;
	if (S_ISDIR(inode->i_mode)) {
		ret = srfs_dir_add_entry(inode, ".", inode);
		if (ret) {
			printk(KERN_ERR "srfs_add_entry %s failed: %d\n", ".", ret);
			return ret;
		}

		ret = srfs_dir_add_entry(inode, "..", dir);
		if (ret) {
			printk(KERN_ERR "srfs_add_entry %s failed: %d\n", "..", ret);
			return ret;
		}
	}

	return 0;
}

static struct dentry *srfs_lookup(struct inode *dir,
								struct dentry *dentry,
								unsigned int flags)
{
	printk("%s <--\n", __func__);
	return srfs_dir_find_entry(dir, dentry, flags);
}
