// SPDX-License-Identifier: GPL-2.0-only
#include "linux/panic.h"
#include <linux/statfs.h>
#include <linux/seq_file.h>
#include <linux/crc32c.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/exportfs.h>
#include <linux/backing-dev.h>
#include "codexfs_fs.h"
#include "internal.h"

static struct kmem_cache *codexfs_inode_cachep __read_mostly;

void _codexfs_printk(struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int level;

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;
	if (sb)
		printk("%c%ccodexfs (device %s): %pV", KERN_SOH_ASCII, level,
		       sb->s_id, &vaf);
	else
		printk("%c%ccodexfs: %pV", KERN_SOH_ASCII, level, &vaf);
	va_end(args);
}

static void codexfs_fc_free(struct fs_context *fc)
{
}

static int codexfs_read_superblock(struct super_block *sb)
{
	struct codexfs_sb_info *sbi = CODEXFS_SB(sb);
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;
	struct codexfs_super_block *dsb;
	void *data;
	int ret = 0;

	data = codexfs_read_metabuf(&buf, sb, 0, CODEXFS_KMAP);
	if (IS_ERR(data)) {
		codexfs_err(sb, "cannot read codexfs superblock");
		return PTR_ERR(data);
	}

	dsb = (struct codexfs_super_block *)(data + CODEXFS_SUPERBLK_OFF);
	if (le32_to_cpu(dsb->magic) != CODEXFS_MAGIC) {
		codexfs_err(sb, "cannot find valid codexfs superblock");
		goto out;
	}

	sbi->blkszbits = dsb->blksz_bits;
	if (sbi->blkszbits < 9 || sbi->blkszbits > PAGE_SHIFT) {
		codexfs_err(sb, "blkszbits %u isn't supported", sbi->blkszbits);
		goto out;
	}

	sbi->islotbits = ilog2(sizeof(struct codexfs_inode));
	sbi->root_nid = le64_to_cpu(dsb->root_nid);
	sbi->inos = le64_to_cpu(dsb->inos);
out:
	codexfs_put_metabuf(&buf);
	return ret;
}

static int codexfs_fc_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct inode *inode;
	struct codexfs_sb_info *sbi = CODEXFS_SB(sb);
	int err;

	sb->s_magic = CODEXFS_MAGIC;
	sb->s_flags |= SB_RDONLY | SB_NOATIME;
	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_flags |= SB_POSIXACL;
	sb->s_op = &codexfs_sops;

	sbi->blkszbits = PAGE_SHIFT;
	if (!sb->s_bdev) {
		BUG();
	} else {
		if (!sb_set_blocksize(sb, PAGE_SIZE)) {
			errorfc(fc, "failed to set initial blksize");
			return -EINVAL;
		}
	}

	err = codexfs_read_superblock(sb);
	if (err)
		return err;

	sb->s_time_gran = 1;

	inode = codexfs_iget(sb, sbi->root_nid);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	if (!S_ISDIR(inode->i_mode)) {
		codexfs_err(sb,
			    "rootino(nid %llu) is not a directory(i_mode %o)",
			    sbi->root_nid, inode->i_mode);
		iput(inode);
		return -EINVAL;
	}
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;

	codexfs_info(sb, "mounted with root inode @ nid %llu.", sbi->root_nid);
	return 0;
}

static int codexfs_fc_get_tree(struct fs_context *fc)
{
	int ret;
	ret = get_tree_bdev(fc, codexfs_fc_fill_super);
	return ret;
}

static const struct fs_context_operations codexfs_context_ops = {
	.get_tree = codexfs_fc_get_tree,
	.free = codexfs_fc_free,
};

static int codexfs_init_fs_context(struct fs_context *fc)
{
	struct codexfs_sb_info *sbi;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	fc->s_fs_info = sbi;
	fc->ops = &codexfs_context_ops;
	return 0;
}

static void codexfs_kill_sb(struct super_block *sb)
{
}

static void codexfs_put_super(struct super_block *sb)
{
}

static struct file_system_type codexfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "codexfs",
	.init_fs_context = codexfs_init_fs_context,
	.kill_sb = codexfs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV | FS_ALLOW_IDMAP,
};
MODULE_ALIAS_FS("codexfs");

static int __init codexfs_module_init(void)
{
	int err;

	err = register_filesystem(&codexfs_fs_type);
	if (err)
		goto fs_err;

	return 0;

fs_err:
	kmem_cache_destroy(codexfs_inode_cachep);
	return err;
}

static void __exit codexfs_module_exit(void)
{
	unregister_filesystem(&codexfs_fs_type);

	kmem_cache_destroy(codexfs_inode_cachep);
}


const struct super_operations codexfs_sops = {
	.put_super = codexfs_put_super,
};

module_init(codexfs_module_init);
module_exit(codexfs_module_exit);

MODULE_DESCRIPTION("CodexFS");
MODULE_AUTHOR("ChenRuiwei");
MODULE_LICENSE("GPL");
