#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

MODULE_DESCRIPTION("Educational file-system");
MODULE_AUTHOR("Kevin J.");
MODULE_LICENSE("GPL");

#define MAGIC 0xdeadbeef
#define DEFAULT_MODE 0775

struct fs_mount_opts {
    umode_t mode;
};

struct fs_info {
    struct fs_mount_opts mount_opts;
};

static const struct super_operations fs_ops = {
    .statfs = simple_statfs,
    .drop_inode = generic_drop_inode
};

static struct inode *fs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev) {
    struct inode *inode = new_inode(sb);

    if (inode) {
        inode->i_ino = get_next_ino();
        inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
    }

    return inode;
}

static int fs_fill_super(struct super_block *sb, void *data, int silent) {
    struct fs_info *fsi;
    struct inode *inode;

    fsi = kzalloc(sizeof(struct fs_info), GFP_KERNEL);
    if (!fsi) 
        return -ENOMEM;

    sb->s_fs_info = fsi;
    sb->s_op = &fs_ops;
    sb->s_maxbytes = MAX_LFS_FILESIZE; 
    sb->s_blocksize = PAGE_SIZE; 
    sb->s_blocksize_bits = PAGE_SHIFT; 
    sb->s_magic = MAGIC;
    sb->s_time_gran = 1; 

    inode = fs_get_inode(sb, NULL, S_IFDIR | DEFAULT_MODE, 0);
    sb->s_root = d_make_root(inode);

    if (!sb->s_root) 
        return -ENOMEM;
    
    return 0;
}

static struct dentry *fs_mount(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data) {
    return mount_nodev(fs_type, flags, data, fs_fill_super);
}

static void fs_kill_sb(struct super_block *sb) {
    kfree(sb->s_fs_info);
    kill_litter_super(sb);
    printk(KERN_INFO "Superblock has been killed\n");
}

static struct file_system_type fs_type = {
    .name = "file_system",
    .mount = fs_mount,
    .kill_sb = fs_kill_sb,
    .owner = THIS_MODULE
};

static int fs_init(void) {
	printk(KERN_INFO "Loading the filesystem module\n");

	int ret = register_filesystem(&fs_type);
	if (ret) {
		printk(KERN_ERR "Failed registering filesystem\n");
		return -1;
	}


	return 0;
}

static void fs_exit(void) {
	printk(KERN_INFO "Unloading the filesystem module\n");

    int ret = unregister_filesystem(&fs_type); 
    if (ret) {
        printk(KERN_ERR "Failed unregistering filesystem\n");
        return;
    }

    return;
}

module_init(fs_init);
module_exit(fs_exit);

