// SPDX-License-Identifier: GPL-2.0-only

#include "asm-generic/errno.h"
#include "internal.h"

static int codexfs_read_inode(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct codexfs_sb_info *sbi = CODEXFS_SB(sb);
	struct codexfs_inode_info *vi = CODEXFS_I(inode);
	const codexfs_off_t inode_loc = codexfs_iloc(inode);
	codexfs_blk_t blkaddr, nblks = 0;
	void *kaddr;
	struct codexfs_inode *di;
	union codexfs_inode_union iu;
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;
	unsigned int ofs;
	int err = 0;

	blkaddr = addr_to_blk_id(sbi, inode_loc);
	ofs = addr_to_blk_off(sbi, inode_loc);

	kaddr = codexfs_read_metabuf(&buf, sb, blk_id_to_addr(sbi, blkaddr),
				     CODEXFS_KMAP);
	if (IS_ERR(kaddr)) {
		codexfs_err(sb, "failed to get inode (nid: %llu) page, err %ld",
			    vi->nid, PTR_ERR(kaddr));
		return PTR_ERR(kaddr);
	}

	di = kaddr + ofs;

	ofs += sizeof(struct codexfs_inode);

	inode->i_mode = le16_to_cpu(di->mode);
	iu = di->u;
	i_uid_write(inode, le16_to_cpu(di->uid));
	i_gid_write(inode, le16_to_cpu(di->gid));
	set_nlink(inode, le16_to_cpu(di->nlink));

	inode->i_size = le32_to_cpu(di->size);

	if (unlikely(inode->i_size < 0)) {
		codexfs_err(sb, "negative i_size @ nid %llu", vi->nid);
		err = -EUCLEAN;
		goto err_out;
	}
	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
	case S_IFDIR:
	case S_IFLNK:
		// vi->raw_blkaddr = le32_to_cpu(iu.blk_off);
		if (S_ISLNK(inode->i_mode)) {
			// err = codexfs_fill_symlink(inode, kaddr, ofs);
			err = 0;
			if (err)
				goto err_out;
		}
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
	default:
		codexfs_err(sb, "bogus i_mode (%o) @ nid %llu", inode->i_mode,
			    vi->nid);
		err = -EUCLEAN;
		goto err_out;
	}

	inode_set_mtime_to_ts(
		inode, inode_set_atime_to_ts(inode, inode_get_ctime(inode)));

	inode->i_flags &= ~S_DAX;

	if (!nblks)
		/* measure inode.i_blocks as generic filesystems */
		inode->i_blocks = round_up(inode->i_size, sb->s_blocksize) >> 9;
	else
		inode->i_blocks = nblks << (sb->s_blocksize_bits - 9);
err_out:
	DBG_BUGON(err);
	codexfs_put_metabuf(&buf);
	return err;
}

static int codexfs_fill_inode(struct inode *inode)
{
	// struct codexfs_inode_info *vi = CODEXFS_I(inode);
	int err;

	/* read inode base data from disk */
	err = codexfs_read_inode(inode);
	if (err)
		return err;

	/* setup the new inode */
	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &codexfs_generic_iops;
		inode->i_fop = &generic_ro_fops;
		break;
	case S_IFDIR:
		inode->i_op = &codexfs_dir_iops;
		inode->i_fop = &codexfs_dir_fops;
		inode_nohighmem(inode);
		break;
	case S_IFLNK:
		if (inode->i_link)
			inode->i_op = &codexfs_fast_symlink_iops;
		else
			inode->i_op = &codexfs_symlink_iops;
		inode_nohighmem(inode);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		inode->i_op = &codexfs_generic_iops;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
		return 0;
	default:
		return -EUCLEAN;
	}

	mapping_set_large_folios(inode->i_mapping);

	return err;
}

/*
 * ino_t is 32-bits on 32-bit arch. We have to squash the 64-bit value down
 * so that it will fit.
 */
static ino_t codexfs_squash_ino(codexfs_nid_t nid)
{
	ino_t ino = (ino_t)nid;

	if (sizeof(ino_t) < sizeof(codexfs_nid_t))
		ino ^= nid >> (sizeof(codexfs_nid_t) - sizeof(ino_t)) * 8;
	return ino;
}

static int codexfs_iget5_eq(struct inode *inode, void *opaque)
{
	return CODEXFS_I(inode)->nid == *(codexfs_nid_t *)opaque;
}

static int codexfs_iget5_set(struct inode *inode, void *opaque)
{
	const codexfs_nid_t nid = *(codexfs_nid_t *)opaque;

	inode->i_ino = codexfs_squash_ino(nid);
	CODEXFS_I(inode)->nid = nid;
	return 0;
}

struct inode *codexfs_iget(struct super_block *sb, codexfs_nid_t nid)
{
	struct inode *inode;

	inode = iget5_locked(sb, codexfs_squash_ino(nid), codexfs_iget5_eq,
			     codexfs_iget5_set, &nid);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		int err = codexfs_fill_inode(inode);

		if (err) {
			iget_failed(inode);
			return ERR_PTR(err);
		}
		unlock_new_inode(inode);
	}
	return inode;
}

const struct inode_operations codexfs_generic_iops = {};

const struct inode_operations codexfs_symlink_iops = {
	.get_link = page_get_link,
};

const struct inode_operations codexfs_fast_symlink_iops = {
	.get_link = simple_get_link,
};
