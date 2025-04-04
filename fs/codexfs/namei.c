// SPDX-License-Identifier: GPL-2.0-only

#include "codexfs_fs.h"
#include "internal.h"
#include "linux/err.h"
#include "linux/slab.h"

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

static struct codexfs_dirent *
find_target_dirent(struct codexfs_qstr *name, u8 *data,
			  unsigned int size, const int ndirents)
{
	struct codexfs_dirent *const de = (struct codexfs_dirent *)data;
	unsigned int matched = 0;
	for (int i = 0; i < ndirents; i++) {
		const int nameoff = de[i].nameoff;
		struct codexfs_qstr dname = {
			.name = data + nameoff,
			.end = i != (ndirents - 1) ? data + de[i + 1].nameoff :
						     data + size
		};
		/* string comparison without already matched prefix */
		int ret = codexfs_dirnamecmp(name, &dname, &matched);
		if (!ret) {
			return de + i;
		}
	}
	return ERR_PTR(-ENOENT);
}

int codexfs_namei(struct inode *dir, const struct qstr *name,
		  codexfs_nid_t *nid, unsigned int *d_type)
{
	int ndirents;
	struct codexfs_dirent *de;
	struct codexfs_qstr qn;
	unsigned int nameoff;
	struct super_block *sb = dir->i_sb;
	struct codexfs_inode_info *vi = CODEXFS_I(dir);
	void * data;

	if (!dir->i_size)
		return -ENOENT;

	qn.name = name->name;
	qn.end = name->name + name->len;

	data = codexfs_read_data(sb, nid_to_inode_meta_off(sb, vi->nid),
			       dir->i_size);
	if (IS_ERR(data)) 
		return PTR_ERR(data);

	de = data;
	nameoff = le16_to_cpu(de->nameoff);
	ndirents = nameoff / sizeof(struct codexfs_dirent);
	if (ndirents)
		de = find_target_dirent(&qn, (u8 *)de, dir->i_size,
					       ndirents);

	if (!IS_ERR(de)) {
		*nid = le64_to_cpu(de->nid);
		*d_type = de->file_type;
	}

	kfree(data);
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
	.getattr = codexfs_getattr,
};
