// SPDX-License-Identifier: GPL-2.0-only
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

