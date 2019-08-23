#include <linux/module.h> 
#include <linux/init.h>
#include <linux/fs.h>

#include "ksrfs.h"

struct dentry* srfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data);

void srfs_kill_sb(struct super_block *sb);

/*
 * srfs stands for simple ram fs
 */
struct file_system_type srfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "srfs",
	.mount = srfs_mount,
	.kill_sb = srfs_kill_sb,
	.fs_flags = FS_USERNS_MOUNT,
};

static int __init srfs_init(void)
{
	int ret;

	ret = register_filesystem(&srfs_fs_type);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Failed to register srfs with error code :%d\n", ret);
	}

	return ret;
}

static void __exit srfs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&srfs_fs_type);
	if (unlikely(ret != 0)) {
		printk(KERN_ERR "Failed to unregister srfs with error code :%d\n", ret);
	}
}

module_init(srfs_init);
module_exit(srfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("accelazh");