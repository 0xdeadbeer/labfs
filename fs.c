#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/time.h>

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

struct generic_inode_info {
    struct inode vfs_inode;
};

static const struct inode_operations dir_inode_operations;
static const struct inode_operations reg_inode_operations;
static const struct file_operations reg_file_operations;
static const struct super_operations fs_operations;

static int simple_read_folio(struct file *file, struct folio *folio) {
    folio_zero_range(folio, 0, folio_size(folio));
    flush_dcache_folio(folio);
    folio_mark_uptodate(folio);
    folio_unlock(folio);
    return 0;
}

static int simple_write_end(struct file *file, struct address_space *mapping, loff_t pos, unsigned len, unsigned copied, struct page *page, void *fsdata) {
    struct folio *folio = page_folio(page);
	struct inode *inode = folio->mapping->host;
	loff_t last_pos = pos + copied;

	/* zero the stale part of the folio if we did a short copy */
	if (!folio_test_uptodate(folio)) {
		if (copied < len) {
			size_t from = offset_in_folio(folio, pos);

			folio_zero_range(folio, from + copied, len - copied);
		}
		folio_mark_uptodate(folio);
	}
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size)
		i_size_write(inode, last_pos);

	folio_mark_dirty(folio);
	folio_unlock(folio);
	folio_put(folio);

	return copied;
}

static const struct address_space_operations aops = {
    .read_folio = simple_read_folio,
    .write_begin = simple_write_begin,
    .write_end = simple_write_end,
    .dirty_folio = noop_dirty_folio
}; 

static struct inode *fs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev) {
    struct inode *inode = new_inode(sb);

    if (inode) {
        inode->i_ino = get_next_ino();
        inode_init_owner(&nop_mnt_idmap, inode, dir, mode);

        inode->i_mapping->a_ops = &aops;
        mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
        mapping_set_unevictable(inode->i_mapping);
        simple_inode_init_ts(inode);

        switch (mode & S_IFMT) {
        case S_IFREG: 
            inode->i_op = &reg_inode_operations;
            inode->i_fop = &reg_file_operations;
            break;
        case S_IFDIR: 
            inode->i_op = &dir_inode_operations;
            inode->i_fop = &simple_dir_operations;

            inc_nlink(inode);
            break;
        }
    }

    return inode;
}

static int fs_mknod(struct mnt_idmap *map, struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev) {
    struct inode *inode = fs_get_inode(dir->i_sb, dir, mode, dev);

    if (!inode)
        return -ENOSPC;

    printk(KERN_INFO "Creating new inode '%p' with dir dentry '%p'\n", inode, dir);

    d_instantiate(dentry, inode);  
    dget(dentry);
    inode_set_mtime_to_ts(dir, inode_set_ctime_current(dir));

    return 0;
}

static int fs_create(struct mnt_idmap *map, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    return fs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFREG, 0);
}

static int fs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
    return fs_mknod(&nop_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
}

static const struct inode_operations reg_inode_operations = {
    .getattr = simple_getattr,
    .setattr = simple_setattr
};

static const struct file_operations reg_file_operations = {
    .owner = THIS_MODULE,
    .open = generic_file_open,
	.read_iter	= generic_file_read_iter,
    .write_iter = generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= filemap_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek
};

static const struct super_operations fs_operations = {
    .statfs = simple_statfs,
    .drop_inode = generic_delete_inode
};

static const struct inode_operations dir_inode_operations = {
    .create = fs_create, 
    .lookup = simple_lookup, 
    .link = simple_link, 
    .unlink = simple_unlink, 
    .mkdir = fs_mkdir,
    .rmdir = simple_rmdir, 
    .mknod = fs_mknod, 
    .rename = simple_rename
};


static int fs_fill_super(struct super_block *sb, void *data, int silent) {
    struct fs_info *fsi;
    struct inode *inode;

    fsi = kzalloc(sizeof(struct fs_info), GFP_KERNEL);
    if (!fsi) 
        return -ENOMEM;

    sb->s_fs_info = fsi;
    sb->s_op = &fs_operations;
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
    .owner = THIS_MODULE,
    .name = "file_system",
    .mount = fs_mount,
    .kill_sb = fs_kill_sb
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

