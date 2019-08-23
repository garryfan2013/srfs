#include <linux/fs.h>
#include <asm/uaccess.h>

#include "ksrfs.h"

static ssize_t srfs_read(struct file *filp,
							char __user *buf,
							size_t len,
							loff_t *ppos);

static ssize_t srfs_write(struct file *filp,
								const char __user *buf,
								size_t len,
								loff_t *ppos);

static int srfs_readdir(struct file *filp,
							void *dirent,
							filldir_t filldir);


const struct file_operations srfs_file_ops = {
	.read = srfs_read,
	.write = srfs_write,
};

const struct file_operations srfs_dir_ops = {
	.readdir = srfs_readdir,
};

extern struct srfs_block_info *srfs_alloc_block(struct super_block *sb, struct inode *inode);

static struct srfs_block_info *get_file_block(struct inode* inode, uint64_t seq)
{
	struct srfs_inode_info *si;
	struct srfs_block_info *bi;
	int i = 0;

	printk(KERN_INFO "get_file_block ok 1\n");
	si = SRFS_INODE(inode);
	if (!si) {
		printk(KERN_ERR "SRFS_INODE failed\n");
		return NULL;
	}

	if (seq >= si->blk_cnt) {
		printk(KERN_ERR "try to get block %llu, but allocated only %llu\n", seq, si->blk_cnt);
		return NULL;
	}

	printk(KERN_INFO "get_file_block ok 2\n");
	bi = list_first_entry(&si->data, struct srfs_block_info, list);
	printk(KERN_INFO "get_file_block ok 3\n");
	for (; i < seq; i++) {
		bi = list_next_entry(bi, list);
	}

	printk(KERN_INFO "get_file_block ok 4\n");
	return bi;
}

static ssize_t srfs_read(struct file *filp, char __user *buf,
	size_t len, loff_t *ppos)
{
	struct super_block *sb;
	struct inode *inode;
	struct srfs_sb_info *sbi;
	struct srfs_inode_info *si;
	struct srfs_group_info *gi;
	struct srfs_block_info *bi;
	uint64_t start_blk, max, left, copy_bytes;
	char *src, *dst;
	int ret;

	inode = filp->f_dentry->d_inode;
	si = SRFS_INODE(inode);
	sb = inode->i_sb;
	sbi = SRFS_SB(sb);

	printk(KERN_INFO "srfs_read <--\n");
	printk(KERN_INFO "srfs_read file[%s]: size=%llu, buf_len=%lu\n",
					filp->f_dentry->d_name.name, si->size, len);

	/* out of scope */
	if (si->size < *ppos + 1) {
		return -EINVAL;
	}

	len = (len > (si->size - *ppos))?(si->size - *ppos) : len;
	gi = GET_GROUP_BY_INODE_ID(sb, inode->i_ino);
	BUG_ON(si->blk_cnt*gi->blk_size < si->size);

	start_blk = (*ppos)/gi->blk_size;
	/* The space left of the start block should be caculated */
	max = gi->blk_size - (*ppos)%gi->blk_size;
	left = len;

	bi = get_file_block(inode, start_blk);
	if (!bi) {
		printk(KERN_ERR "srfs_read: get file block[%llu] failed\n", start_blk);
		return -EINVAL;
	}

	src = bi->addr + gi->blk_size - max;
	dst = buf;
	copy_bytes = (max > len)?len : max;

	while(1) {
		ret = copy_to_user(dst, src, copy_bytes);
		if (unlikely(ret)) {
			printk("copy_to_user returned %d bytes, expecting %llu bytes\n",
					ret, copy_bytes);
			if (copy_bytes == ret) {
				break;
			}
			copy_bytes -= ret;
		}

		*ppos += copy_bytes;
		left -= copy_bytes;
		if (left <= 0) {
			break;
		}

		bi = list_next_entry(bi, list);
		src = bi->addr;
		dst += copy_bytes;
		copy_bytes = (left > gi->blk_size) ? gi->blk_size : left;
	}

	if (len > left) {
		return len - left;
	}

	return -EFAULT;
}

static ssize_t srfs_write(struct file *filp, const char __user *buf,
	size_t len, loff_t *ppos)
{
	struct super_block *sb;
	struct inode *inode;
	struct srfs_sb_info *sbi;
	struct srfs_inode_info *si;
	struct srfs_group_info *gi;
	struct srfs_block_info *bi;
	uint64_t start_blk, max, left, copy_bytes;
	int ret;
	const char *src, *dst;

	printk("%s <--\n", __func__);
	printk(KERN_INFO "srfs_write: offset=%llu, len=%lu\n", *ppos, len);

	inode = filp->f_dentry->d_inode;
	si = SRFS_INODE(inode);
	sb = inode->i_sb;
	sbi = SRFS_SB(sb);

	ret = generic_write_checks(filp, ppos, &len, 0);
	if (ret) {
		return ret;
	}

	if (GET_GROUP_INDEX(inode->i_ino) != 0) {
		printk(KERN_INFO "gourp id =%lu\n", GET_GROUP_INDEX(inode->i_ino));
		return -EINVAL;
	}

	gi = GET_GROUP_BY_INODE_ID(sb, inode->i_ino);
	BUG_ON(si->blk_cnt*gi->blk_size < si->size);

	printk(KERN_INFO "gi[%llu] - blk_cnt:%llu blk_size:%llu\n",
						gi->id, gi->blk_cnt, gi->blk_size);

	start_blk = (*ppos)/gi->blk_size;

	/* The space left of the start block should be caculated */
	max = gi->blk_size - (*ppos)%gi->blk_size;
	left = len;

    printk(KERN_INFO "start_blk:%llu, max:%llu, left:%llu\n", start_blk, max, left);

    /* try to allocate enough block for writing */
    while(start_blk >= si->blk_cnt) {
    	if (!srfs_alloc_block(sb, inode)) {
    		printk(KERN_ERR "srfs_write alloc block failed\n");
    		return -ENOMEM;
    	}
    }

	bi = get_file_block(inode, start_blk);
	if (!bi) {
		printk(KERN_ERR "Can't get file data block!\n");
		return -EINVAL;
	}

	printk(KERN_INFO "bi_addr: %p block[%llu] - addr:%p\n", bi, bi->id, bi->addr);

	src = buf;
	dst = bi->addr + gi->blk_size - max;
	copy_bytes = (max > len)?len : max;

	printk(KERN_INFO "ready to copy data, src:%p, dst:%p, len:%llu\n", src, dst, copy_bytes);

	while(1) {
		printk(KERN_INFO "copy_from_user dst:%p src:%p len:%llu\n",dst, src, copy_bytes);
		ret = copy_from_user((void *)dst, src, copy_bytes);
		if (unlikely(ret)) {
			printk(KERN_ERR "copy_from_user not enough bytes returned(%d), expecting(%llu)",
							ret, copy_bytes);
			if (ret == copy_bytes) {
				break;
			}
			copy_bytes -= ret;
		}

		si->size += copy_bytes;
		left -= copy_bytes;
		if (left <= 0) {
			break;
		}

		/* maybe need to request a new block? */
		if (++start_blk > si->blk_cnt - 1) {
			if (!srfs_alloc_block(sb, inode)) {
				break;
			}
		}

		bi = list_next_entry(bi, list);
		dst = bi->addr;
		src += copy_bytes;
		copy_bytes = (left > gi->blk_size) ? gi->blk_size : left;
	}

	if (len > left) {
		return len - left;
	}

	return -EFAULT;
}

static int srfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct super_block *sb;
	struct inode *inode;
	struct srfs_sb_info *sbi;
	struct srfs_inode_info *si;
	struct srfs_block_info *bi;
	int ret;
	dir_entry_head_t *eh;
	char *ename;

	inode = filp->f_dentry->d_inode;
	si = SRFS_INODE(inode);
	sb = inode->i_sb;
	sbi = SRFS_SB(sb);

	printk(KERN_INFO "srfs_readdir %s offset(%llu) <---\n", filp->f_dentry->d_name.name, filp->f_pos);

	if (unlikely(!S_ISDIR(inode->i_mode))) {
		printk(KERN_ERR "Can't readdir with a none directory inode\n");
		return -ENOTDIR;
	}

	if (filp->f_pos >= si->size) {
		printk(KERN_WARNING "File position(%llu) exceeds file size(%llu) boundary\n", filp->f_pos, si->size);
		return 0;
	}

	bi = list_first_entry(&si->data, struct srfs_block_info, list);
	
	while(1) {
		eh = (dir_entry_head_t *)(bi->addr + filp->f_pos);
		ename = (char *)(eh + 1);

		printk(KERN_INFO "filldir: name=%s len=%llu pos=%llu ino=%llu\n",
						ename, eh->length, filp->f_pos, eh->ino);
		ret = filldir(dirent, ename, eh->length, filp->f_pos, eh->ino, DT_UNKNOWN);
		if (ret) {
			printk(KERN_INFO "filldir return: %d\n", ret);
			break;		
		}

		filp->f_pos += sizeof(*eh) + eh->length;
		if (filp->f_pos >= si->size) {
			break;
		}
	}

	return 0;
}

