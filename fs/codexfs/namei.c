// SPDX-License-Identifier: GPL-2.0-only
#include "asm-generic/errno.h"
#include "internal.h"

struct codexfs_qstr {
	const unsigned char *name;
	const unsigned char *end;
};

/* based on the end of qn is accurate and it must have the trailing '\0' */
static inline int codexfs_dirnamecmp(const struct codexfs_qstr *qn,
				     const struct codexfs_qstr *qd,
				     unsigned int *matched)
{
	unsigned int i = *matched;

	/*
	 * on-disk error, let's only BUG_ON in the debugging mode.
	 * otherwise, it will return 1 to just skip the invalid name
	 * and go on (in consideration of the lookup performance).
	 */
	DBG_BUGON(qd->name > qd->end);

	/* qd could not have trailing '\0' */
	/* However it is absolutely safe if < qd->end */
	while (qd->name + i < qd->end && qd->name[i] != '\0') {
		if (qn->name[i] != qd->name[i]) {
			*matched = i;
			return qn->name[i] > qd->name[i] ? 1 : -1;
		}
		++i;
	}
	*matched = i;
	/* See comments in __d_alloc on the terminating NUL character */
	return qn->name[i] == '\0' ? 0 : 1;
}

#define nameoff_from_disk(off, sz) (le16_to_cpu(off) & ((sz) - 1))

static struct codexfs_dirent *find_target_dirent(struct codexfs_qstr *name,
						 u8 *data,
						 unsigned int dirblksize,
						 const int ndirents)
{
	int head, back;
	unsigned int startprfx, endprfx;
	struct codexfs_dirent *const de = (struct codexfs_dirent *)data;

	/* since the 1st dirent has been evaluated previously */
	head = 1;
	back = ndirents - 1;
	startprfx = endprfx = 0;

	while (head <= back) {
		const int mid = head + (back - head) / 2;
		const int nameoff =
			nameoff_from_disk(de[mid].nameoff, dirblksize);
		unsigned int matched = min(startprfx, endprfx);
		struct codexfs_qstr dname = {
			.name = data + nameoff,
			.end = mid >= ndirents - 1 ?
				       data + dirblksize :
				       data + nameoff_from_disk(
						      de[mid + 1].nameoff,
						      dirblksize)
		};

		/* string comparison without already matched prefix */
		int ret = codexfs_dirnamecmp(name, &dname, &matched);

		if (!ret) {
			return de + mid;
		} else if (ret > 0) {
			head = mid + 1;
			startprfx = matched;
		} else {
			back = mid - 1;
			endprfx = matched;
		}
	}

	return ERR_PTR(-ENOENT);
}

static void *codexfs_find_target_block(struct codexfs_buf *target,
				       struct inode *dir,
				       struct codexfs_qstr *name,
				       int *_ndirents)
{
	unsigned int bsz = i_blocksize(dir);
	int head = 0, back = codexfs_iblks(dir) - 1;
	struct codexfs_sb_info *sbi = CODEXFS_SB(dir->i_sb);
	unsigned int startprfx = 0, endprfx = 0;
	void *candidate = ERR_PTR(-ENOENT);

	while (head <= back) {
		const int mid = head + (back - head) / 2;
		struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;
		struct codexfs_dirent *de;

		buf.mapping = dir->i_mapping;
		de = codexfs_bread(&buf, blk_id_to_addr(sbi, mid),
				   CODEXFS_KMAP);
		if (!IS_ERR(de)) {
			const int nameoff = nameoff_from_disk(de->nameoff, bsz);
			const int ndirents = nameoff / sizeof(*de);
			int diff;
			unsigned int matched;
			struct codexfs_qstr dname;

			if (!ndirents) {
				codexfs_put_metabuf(&buf);
				codexfs_err(dir->i_sb,
					    "corrupted dir block %d @ nid %llu",
					    mid, CODEXFS_I(dir)->nid);
				DBG_BUGON(1);
				de = ERR_PTR(-EUCLEAN);
				goto out;
			}

			matched = min(startprfx, endprfx);

			dname.name = (u8 *)de + nameoff;
			if (ndirents == 1)
				dname.end = (u8 *)de + bsz;
			else
				dname.end =
					(u8 *)de +
					nameoff_from_disk(de[1].nameoff, bsz);

			/* string comparison without already matched prefix */
			diff = codexfs_dirnamecmp(name, &dname, &matched);

			if (diff < 0) {
				codexfs_put_metabuf(&buf);
				back = mid - 1;
				endprfx = matched;
				continue;
			}

			if (!IS_ERR(candidate))
				codexfs_put_metabuf(target);
			*target = buf;
			if (!diff) {
				*_ndirents = 0;
				return de;
			}
			head = mid + 1;
			startprfx = matched;
			candidate = de;
			*_ndirents = ndirents;
			continue;
		}
out: /* free if the candidate is valid */
		if (!IS_ERR(candidate))
			codexfs_put_metabuf(target);
		return de;
	}
	return candidate;
}

int codexfs_namei(struct inode *dir, const struct qstr *name,
		  codexfs_nid_t *nid, unsigned int *d_type)
{
	int ndirents;
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;
	struct codexfs_dirent *de;
	struct codexfs_qstr qn;

	if (!dir->i_size)
		return -ENOENT;

	qn.name = name->name;
	qn.end = name->name + name->len;
	buf.mapping = dir->i_mapping;

	ndirents = 0;
	de = codexfs_find_target_block(&buf, dir, &qn, &ndirents);
	if (IS_ERR(de))
		return PTR_ERR(de);

	if (ndirents)
		de = find_target_dirent(&qn, (u8 *)de, i_blocksize(dir),
					ndirents);

	if (!IS_ERR(de)) {
		*nid = le64_to_cpu(de->nid);
		*d_type = de->file_type;
	}
	codexfs_put_metabuf(&buf);
	return PTR_ERR_OR_ZERO(de);
}

static struct dentry *codexfs_lookup(struct inode *dir, struct dentry *dentry,
				     unsigned int flags)
{
	int err;
	codexfs_nid_t nid;
	unsigned int d_type;
	struct inode *inode;

	if (dentry->d_name.len > CODEXFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	err = codexfs_namei(dir, &dentry->d_name, &nid, &d_type);

	if (err == -ENOENT)
		/* negative dentry */
		inode = NULL;
	else if (err)
		inode = ERR_PTR(err);
	else
		inode = codexfs_iget(dir->i_sb, nid);
	return d_splice_alias(inode, dentry);
}

const struct inode_operations codexfs_dir_iops = {
	.lookup = codexfs_lookup,
	// .getattr = codexfs_getattr,
	// .listxattr = codexfs_listxattr,
	// .get_inode_acl = codexfs_get_acl,
	// .fiemap = codexfs_fiemap,
};
