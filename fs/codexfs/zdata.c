// SPDX-License-Identifier: GPL-2.0-only

#include "asm/bug.h"
#include "codexfs_fs.h"
#include "internal.h"
#include "linux/fs.h"
#include "linux/printk.h"
#include "linux/slab.h"
#include "vdso/page.h"
#include <linux/psi.h>
#include <linux/cpuhotplug.h>
#include <linux/xz.h>

static int find_extent(struct codexfs_extent *extents, int nextents,
		       codexfs_off_t off)
{
	int i;
	struct codexfs_extent *e;

	for (i = nextents - 1; i >= 0; i--) {
		e = extents + i;
		if (e->off <= off)
			return i;
	}
	BUG();
}

static int z_codexfs_fixup_insize(struct xz_buf *xz_buf, const char *padbuf,
				  unsigned int padbufsize)
{
	const char *padend;

	padend = memchr_inv(padbuf, 0, padbufsize);
	if (!padend)
		return -EFSCORRUPTED;
	xz_buf->in_size -= padend - padbuf;
	xz_buf->in += padend - padbuf;
	printk("padend %ld", padend - padbuf);
	return 0;
}

static int z_codexfs_decompress_block(uint8_t *data, int blksz, uint8_t *out,
				      int outsz)
{
	const uint32_t DICT_SIZE = 1024 * 1024;
	enum xz_ret xz_err;
	int err;
	struct xz_dec_microlzma *s =
		xz_dec_microlzma_alloc(XZ_SINGLE, DICT_SIZE);
	struct xz_buf xz_buf = {
		.in = data,
		.in_pos = 0,
		.in_size = blksz,
		.out = out,
		.out_pos = 0,
		.out_size = outsz,
	};

	err = z_codexfs_fixup_insize(&xz_buf, data, blksz);
	if (err)
		return err;

	xz_dec_microlzma_reset(s, xz_buf.in_size, outsz, false);
	printk("xzbuf inpos %zu, insize %zu, outpos %zu, outsize %zu\n",
	       xz_buf.in_pos, xz_buf.in_size, xz_buf.out_pos, xz_buf.out_size);
	do {
		xz_err = xz_dec_microlzma_run(s, &xz_buf);
		// printk("xzbuf inpos %zu, insize %zu, outpos %zu, outsize %zu",
		//        xz_buf.in_pos, xz_buf.in_size, xz_buf.out_pos,
		//        xz_buf.out_size);
		if (xz_err != XZ_OK) {
			if (xz_err == XZ_STREAM_END || xz_err == XZ_DATA_ERROR)
				break;
			printk("failed to decompress %d\n", xz_err);
			err = -EFSCORRUPTED;
			break;
		}
	} while (1);

	xz_dec_microlzma_end(s);
	return err;
}

static int z_codexfs_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct super_block *sb = inode->i_sb;
	loff_t pos = folio_pos(folio);
	size_t len = min_t(size_t, folio_size(folio), inode->i_size - pos);
	struct codexfs_inode_info *vi = CODEXFS_I(inode);
	codexfs_off_t addr;
	void *data;
	void *edata;
	struct codexfs_extent *e;
	int nextents = vi->blks;
	int i;
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;
	int err = 0;
	size_t len_covered = 0;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		addr = nid_to_inode_meta_off(sb, vi->nid);
		edata = codexfs_read_data(
			sb, addr, nextents * sizeof(struct codexfs_extent));
		e = edata;
		if (IS_ERR(edata))
			return PTR_ERR(edata);
		i = find_extent(e, nextents, pos);
		e += i;
		codexfs_err(sb, "i %d", i);
		codexfs_info(sb, "pos %lld, len %zu", pos, len);

		const uint32_t OUTSZ = 64 * 1024;
		u8 *out = kmalloc(OUTSZ, GFP_KERNEL);
		while (len_covered < len) {
			data = codexfs_read_metabuf(
				&buf, sb, blk_id_to_addr(sb, vi->blk_id + i),
				CODEXFS_KMAP);

			err = z_codexfs_decompress_block(data, PAGE_SIZE, out,
							 OUTSZ);
			if (err)
				return err;
			size_t len_adv;
			size_t start_e_off = pos > e->off ? pos - e->off : 0;
			if (i < nextents - 1) {
				len_adv = min_t(size_t,
						(e + 1)->off - e->off -
							start_e_off,
						len - len_covered);
			} else {
				len_adv = min_t(size_t,
						inode->i_size - e->off -
							start_e_off,
						len - len_covered);
			}
			codexfs_info(sb, "len_adv %zu", len_adv);
			memcpy_to_folio(
				folio,
				offset_in_folio(folio, pos + len_covered),
				out + e->frag_off + start_e_off, len_adv);
			len_covered += len_adv;
			i += 1;
			e += 1;
		}

		codexfs_put_metabuf(&buf);
		kfree(edata);
		kfree(out);
		break;
	case S_IFDIR:
	case S_IFLNK:
		addr = nid_to_inode_meta_off(sb, vi->nid);
		data = codexfs_read_data(sb, addr + pos, len);
		if (IS_ERR(data))
			return PTR_ERR(data);
		memcpy_to_folio(folio, offset_in_folio(folio, pos), data, len);
		kfree(data);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
	default:
		return -EFSCORRUPTED;
	}

	folio_mark_uptodate(folio);

	folio_unlock(folio);
	return 0;
}

const struct address_space_operations z_codexfs_aops = {
	.read_folio = z_codexfs_read_folio,
};
