// SPDX-License-Identifier: GPL-2.0-only
#include "asm/bug.h"
#include "codexfs_fs.h"
#include "linux/gfp_types.h"
#include "linux/math.h"
#include "linux/mm_types.h"
#include "linux/slab.h"
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

void *codexfs_read_multipages(struct codexfs_buf *buf, codexfs_off_t offset,
			      codexfs_size_t len, enum codexfs_kmap_type type)
{
	pgoff_t start_index = offset >> PAGE_SHIFT;
	unsigned int nr_pages = round_up(offset + len, PAGE_SIZE) >>
				PAGE_SHIFT; // 要读取的页数
	struct page **pages = (struct page **)kmalloc_array(
		nr_pages, sizeof(struct page *), GFP_KERNEL);
	struct page **page;

	for (int i = 0; i < nr_pages; i++) {
		page = pages + i;
		*page = read_mapping_page(buf->mapping, start_index + i, NULL);
		if (IS_ERR(page)) {
			BUG();
		}
	}

	// 将多个 page 的物理页映射到连续的虚拟地址
	buf->base = vmap(pages, nr_pages, VM_MAP, PAGE_KERNEL);
	if (!buf->base) {
		BUG();
	}
	return buf->base;
	// // 使用数据
	// memcpy(buffer, vaddr, nr_pages * PAGE_SIZE);
	//
	// // 解除映射并释放资源
	// vunmap(vaddr);
	// for (int i = 0; i < nr_pages; i++)
	// 	folio_put(folios[i]);
}

static int codexfs_iomap_begin(struct inode *inode, loff_t offset,
			       loff_t length, unsigned int flags,
			       struct iomap *iomap, struct iomap *srcmap)
{
	struct super_block *sb = inode->i_sb;
	void *ptr;
	struct codexfs_buf buf = __CODEXFS_BUF_INITIALIZER;

	switch (inode->i_mode & S_IFMT) {
	case S_IFREG:
		BUG();
		break;
	case S_IFDIR:
		iomap->type = IOMAP_INLINE;
		ptr = codexfs_read_metabuf(&buf, sb, codexfs_iloc_meta(inode),
					   CODEXFS_KMAP);
		if (IS_ERR(ptr))
			return PTR_ERR(ptr);
		iomap->offset = offset;
		iomap->bdev = sb->s_bdev;
		iomap->length = length;
		iomap->flags = 0;
		iomap->private = NULL;
		iomap->inline_data = ptr;
		iomap->private = buf.base;
		break;
	case S_IFLNK:
		BUG();
		break;
	case S_IFCHR:
	case S_IFBLK:
	case S_IFIFO:
	case S_IFSOCK:
		BUG();
		break;
	default:
		return -EUCLEAN;
	}

	return 0;
}

static int codexfs_iomap_end(struct inode *inode, loff_t pos, loff_t length,
			     ssize_t written, unsigned int flags,
			     struct iomap *iomap)
{
	void *ptr = iomap->private;

	if (ptr) {
		struct codexfs_buf buf = {
			.page = kmap_to_page(ptr),
			.base = ptr,
		};

		DBG_BUGON(iomap->type != IOMAP_INLINE);
		codexfs_put_metabuf(&buf);
	} else {
		DBG_BUGON(iomap->type == IOMAP_INLINE);
	}
	return written;
}

static const struct iomap_ops codexfs_iomap_ops = {
	.iomap_begin = codexfs_iomap_begin,
	.iomap_end = codexfs_iomap_end,
};

/*
 * since we dont have write or truncate flows, so no inode
 * locking needs to be held at the moment.
 */
static int codexfs_read_folio(struct file *file, struct folio *folio)
{
	return iomap_read_folio(folio, &codexfs_iomap_ops);
}

static void codexfs_readahead(struct readahead_control *rac)
{
	return iomap_readahead(rac, &codexfs_iomap_ops);
}

static sector_t codexfs_bmap(struct address_space *mapping, sector_t block)
{
	return iomap_bmap(mapping, block, &codexfs_iomap_ops);
}

/* for uncompressed (aligned) files and raw access for other files */
const struct address_space_operations codexfs_aops = {
	.read_folio = codexfs_read_folio,
	.readahead = codexfs_readahead,
	.bmap = codexfs_bmap,
	.direct_IO = noop_direct_IO,
	.release_folio = iomap_release_folio,
	.invalidate_folio = iomap_invalidate_folio,
};
