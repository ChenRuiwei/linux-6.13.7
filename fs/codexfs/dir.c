// SPDX-License-Identifier: GPL-2.0-only
#include "asm-generic/errno.h"
#include "asm/bug.h"
#include "internal.h"

static int codexfs_fill_dentries(struct inode *dir, struct dir_context *ctx,
				 void *dentry_blk, struct codexfs_dirent *de,
				 unsigned int nameoff0, unsigned int maxsize)
{
	const struct codexfs_dirent *end = dentry_blk + nameoff0;

	while (de < end) {
		unsigned char d_type = fs_ftype_to_dtype(de->file_type);
		unsigned int nameoff = le16_to_cpu(de->nameoff);
		const char *de_name = (char *)dentry_blk + nameoff;
		unsigned int de_namelen;

		/* the last dirent in the block? */
		if (de + 1 >= end)
			de_namelen = strnlen(de_name, maxsize - nameoff);
		else
			de_namelen = le16_to_cpu(de[1].nameoff) - nameoff;

		/* a corrupted entry is found */
		if (nameoff + de_namelen > maxsize ||
		    de_namelen > CODEXFS_NAME_LEN) {
			codexfs_err(dir->i_sb, "bogus dirent @ nid %llu",
				    CODEXFS_I(dir)->nid);
			DBG_BUGON(1);
			return -EIO;
		}

		if (!dir_emit(ctx, de_name, de_namelen, le64_to_cpu(de->nid),
			      d_type))
			return 1;
		++de;
		ctx->pos += sizeof(struct codexfs_dirent);
	}
	return 0;
}

static int codexfs_readdir(struct file *f, struct dir_context *ctx)
{
	struct inode *dir = file_inode(f);
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;
	int err = 0;
	struct codexfs_dirent *de;
	unsigned int nameoff;

	// FIXME: release multipages
	buf.mapping = dir->i_mapping;
	de = codexfs_read_multipages(&buf, 0, dir->i_size, CODEXFS_KMAP);
	nameoff = le16_to_cpu(de->nameoff);
	if (ctx->pos < nameoff) {
		err = codexfs_fill_dentries(dir, ctx, de, de, nameoff, dir->i_size);
		if (err)
			BUG();
	}
	codexfs_put_metabuf(&buf);
	return err < 0 ? err : 0;
}

const struct file_operations codexfs_dir_fops = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate_shared = codexfs_readdir,
};
