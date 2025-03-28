/* SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0 */

#ifndef __CODEXFS_INTERNAL_H
#define __CODEXFS_INTERNAL_H

#include <linux/fs.h>
#include <linux/dax.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/iomap.h>
#include "codexfs_fs.h"

#define DBG_BUGON BUG_ON

__printf(2, 3) void _codexfs_printk(struct super_block *sb, const char *fmt,
				    ...);
#define codexfs_err(sb, fmt, ...) \
	_codexfs_printk(sb, KERN_ERR fmt "\n", ##__VA_ARGS__)
#define codexfs_info(sb, fmt, ...) \
	_codexfs_printk(sb, KERN_INFO fmt "\n", ##__VA_ARGS__)

struct codexfs_device_info {
	char *path;
	struct file *file;
};

struct codexfs_sb_info {
	struct codexfs_device_info dif0;

	unsigned char islotbits; /* inode slot unit size in bit shift */
	unsigned char blkszbits; /* filesystem block size in bit shift */
	/* what we really care is nid, rather than ino.. */
	codexfs_nid_t root_nid;
	u64 inos;
};

#define CODEXFS_SB(sb) ((struct codexfs_sb_info *)(sb)->s_fs_info)
#define CODEXFS_I_SB(inode) ((struct codexfs_sb_info *)(inode)->i_sb->s_fs_info)

struct codexfs_inode_info {
	codexfs_nid_t nid;

	/* atomic flags (including bitlocks) */
	unsigned long flags;

	/* the corresponding vfs inode */
	struct inode vfs_inode;
};

#define CODEXFS_I(ptr) container_of(ptr, struct codexfs_inode_info, vfs_inode)

enum codexfs_kmap_type {
	CODEXFS_NO_KMAP, /* don't map the buffer */
	CODEXFS_KMAP, /* use kmap_local_page() to map the buffer */
};

struct codexfs_buf {
	struct address_space *mapping;
	struct file *file;
	struct page *page;
	void *base;
};
#define __CODEXFS_BUF_INITIALIZER ((struct codexfs_buf){ .page = NULL })

extern const struct super_operations codexfs_sops;
extern const struct inode_operations codexfs_generic_iops;
extern const struct inode_operations codexfs_symlink_iops;
extern const struct inode_operations codexfs_fast_symlink_iops;
extern const struct inode_operations codexfs_dir_iops;
extern const struct file_operations codexfs_dir_fops;

#define codexfs_iblks(i)	(round_up((i)->i_size, i_blocksize(i)) >> (i)->i_blkbits)

static inline codexfs_blk_t addr_to_blk_id(struct codexfs_sb_info *sb,
					   __u64 addr)
{
	return addr >> sb->blkszbits;
}
static inline __u64 blk_id_to_addr(struct codexfs_sb_info *sb,
				   codexfs_blk_t blk_id)
{
	return blk_id << sb->blkszbits;
}
static inline codexfs_blk_off_t addr_to_blk_off(struct codexfs_sb_info *sb,
						__u64 addr)
{
	return addr & ((1 << sb->blkszbits) - 1);
}
static inline __u64 addr_to_nid(struct codexfs_sb_info *sb, __u64 addr)
{
	return addr >> sb->islotbits;
}
static inline __u64 nid_to_inode_off(struct codexfs_sb_info *sb,
				     codexfs_nid_t nid)
{
	return nid << sb->islotbits;
}
static inline __u64 nid_to_inode_meta_off(struct codexfs_sb_info *sb,
					  codexfs_nid_t nid)
{
	return (nid + 1) << sb->islotbits;
}

static inline codexfs_off_t codexfs_iloc(struct inode *inode)
{
	struct codexfs_sb_info *sbi = CODEXFS_I_SB(inode);
	return nid_to_inode_off(sbi, CODEXFS_I(inode)->nid);
}

void *codexfs_read_metadata(struct super_block *sb, struct codexfs_buf *buf,
			    codexfs_off_t *offset, int *lengthp);
void codexfs_unmap_metabuf(struct codexfs_buf *buf);
void codexfs_put_metabuf(struct codexfs_buf *buf);
void *codexfs_bread(struct codexfs_buf *buf, codexfs_off_t offset,
		    enum codexfs_kmap_type type);
void codexfs_init_metabuf(struct codexfs_buf *buf, struct super_block *sb);
void *codexfs_read_metabuf(struct codexfs_buf *buf, struct super_block *sb,
			   codexfs_off_t offset, enum codexfs_kmap_type type);
struct inode *codexfs_iget(struct super_block *sb, codexfs_nid_t nid);
int codexfs_namei(struct inode *dir, const struct qstr *name,
		  codexfs_nid_t *nid, unsigned int *d_type);

#endif
