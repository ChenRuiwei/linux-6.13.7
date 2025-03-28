// SPDX-License-Identifier: GPL-2.0-only
#include "asm-generic/errno.h"
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
	struct super_block *sb = dir->i_sb;
	struct codexfs_sb_info *sbi = CODEXFS_SB(sb);
	unsigned long bsz = sb->s_blocksize;
	unsigned int ofs = addr_to_blk_off(sbi, ctx->pos);
	int err = 0;
	bool initial = true;

	buf.mapping = dir->i_mapping;
	while (ctx->pos < dir->i_size) {
		codexfs_off_t dbstart = ctx->pos - ofs;
		struct codexfs_dirent *de;
		unsigned int nameoff, maxsize;

		de = codexfs_bread(&buf, dbstart, CODEXFS_KMAP);
		if (IS_ERR(de)) {
			codexfs_err(
				sb,
				"fail to readdir of logical block %u of nid %llu",
				addr_to_blk_id(sbi, dbstart),
				CODEXFS_I(dir)->nid);
			err = PTR_ERR(de);
			break;
		}

		nameoff = le16_to_cpu(de->nameoff);
		if (nameoff < sizeof(struct codexfs_dirent) || nameoff >= bsz) {
			codexfs_err(sb, "invalid de[0].nameoff %u @ nid %llu",
				    nameoff, CODEXFS_I(dir)->nid);
			err = -EUCLEAN;
			break;
		}

		maxsize = min_t(unsigned int, dir->i_size - dbstart, bsz);
		/* search dirents at the arbitrary position */
		if (initial) {
			initial = false;
			ofs = roundup(ofs, sizeof(struct codexfs_dirent));
			ctx->pos = dbstart + ofs;
		}

		err = codexfs_fill_dentries(dir, ctx, de, (void *)de + ofs,
					    nameoff, maxsize);
		if (err)
			break;
		ctx->pos = dbstart + maxsize;
		ofs = 0;
	}
	codexfs_put_metabuf(&buf);
	return err < 0 ? err : 0;
}

const struct file_operations codexfs_dir_fops = {
	.llseek = generic_file_llseek,
	.read = generic_read_dir,
	.iterate_shared = codexfs_readdir,
};
