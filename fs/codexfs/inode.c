// SPDX-License-Identifier: GPL-2.0-only

#include "internal.h"

static int codexfs_fill_inode(struct inode *inode)
{
	return 0;
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
