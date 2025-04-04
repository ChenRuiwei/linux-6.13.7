// SPDX-License-Identifier: GPL-2.0-only
#include "asm/bug.h"
#include "codexfs_fs.h"
#include "linux/err.h"
#include "linux/fs.h"
#include "linux/gfp_types.h"
#include "linux/highmem.h"
#include "linux/iomap.h"
#include "linux/minmax.h"
#include "linux/mm.h"
#include "linux/mm_types.h"
#include "linux/pagemap.h"
#include "linux/slab.h"
#include "linux/types.h"
#include "vdso/page.h"
#include <linux/sched/mm.h>
#include "internal.h"

void codexfs_unmap_metabuf(struct codexfs_buf *buf)
{
	if (!buf->base)
		return;
	kunmap_local(buf->base);
	buf->base = NULL;
}

void codexfs_put_metabuf(struct codexfs_buf *buf)
{
	if (!buf->page)
		return;
	codexfs_unmap_metabuf(buf);
	folio_put(page_folio(buf->page));
	buf->page = NULL;
}

void *codexfs_bread(struct codexfs_buf *buf, codexfs_off_t offset,
		    enum codexfs_kmap_type type)
{
	pgoff_t index = offset >> PAGE_SHIFT;
	struct folio *folio = NULL;

	if (buf->page) {
		folio = page_folio(buf->page);
		if (folio_file_page(folio, index) != buf->page)
			codexfs_unmap_metabuf(buf);
	}
	if (!folio || !folio_contains(folio, index)) {
		codexfs_put_metabuf(buf);
		folio = read_mapping_folio(buf->mapping, index, buf->file);
		if (IS_ERR(folio))
			return folio;
	}
	buf->page = folio_file_page(folio, index);
	if (!buf->base && type == CODEXFS_KMAP)
		buf->base = kmap_local_page(buf->page);
	if (type == CODEXFS_NO_KMAP)
		return NULL;
	return buf->base + (offset & ~PAGE_MASK);
}

void codexfs_init_metabuf(struct codexfs_buf *buf, struct super_block *sb)
{
	buf->file = NULL;
	buf->mapping = sb->s_bdev->bd_mapping;
}

void *codexfs_read_metabuf(struct codexfs_buf *buf, struct super_block *sb,
			   codexfs_off_t offset, enum codexfs_kmap_type type)
{
	codexfs_init_metabuf(buf, sb);
	return codexfs_bread(buf, offset, type);
}

void *codexfs_read_data(struct super_block *sb, codexfs_off_t addr, int len)
{
	u8 *buffer, *ptr;
	int i, cnt;
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;

	codexfs_init_metabuf(&buf, sb);
	buffer = kmalloc(len, GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < len; i += cnt) {
		cnt = min_t(int, sb->s_blocksize - addr_to_blk_off(sb, addr),
			    len - i);
		ptr = codexfs_bread(&buf, addr + i, CODEXFS_KMAP);
		if (IS_ERR(ptr)) {
			kfree(buffer);
			codexfs_put_metabuf(&buf);
			return ptr;
		}
		memcpy(buffer + i, ptr, cnt);
	}

	codexfs_put_metabuf(&buf);
	return buffer;
}

static int codexfs_read_folio(struct file *file, struct folio *folio)
{
	struct inode *inode = folio->mapping->host;
	struct super_block *sb = inode->i_sb;
	loff_t pos = folio_pos(folio);
	size_t len = folio_size(folio);
	struct codexfs_inode_info *vi = CODEXFS_I(inode);
	codexfs_off_t addr;
	void *data;

	codexfs_info(sb, "pos %lld, len %zu", pos, len);

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		addr = blk_id_to_addr(sb, vi->blk_id) + vi->blk_off;
		break;
	case S_IFDIR:
	case S_IFLNK:
		addr = nid_to_inode_meta_off(sb, vi->nid);
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
	default:
		return -EUCLEAN;
	}

	len = min_t(size_t, len, inode->i_size - pos);
	codexfs_info(sb, "addr %lld, len %zu", addr, len);
	data = codexfs_read_data(sb, addr + pos, len);
	if (IS_ERR(data))
		return PTR_ERR(data);
	codexfs_info(sb, "offset in folio %zu", offset_in_folio(folio, pos));
	memcpy_to_folio(folio, offset_in_folio(folio, pos), data, len);
	folio_mark_uptodate(folio);

	folio_unlock(folio);
	kfree(data);
	return 0;
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations codexfs_aops = {
	.read_folio = codexfs_read_folio,
};
