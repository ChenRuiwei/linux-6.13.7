/* SPDX-License-Identifier: GPL-2.0-only OR Apache-2.0 */

#ifndef __CODEXFS_H
#define __CODEXFS_H

#include "linux/build_bug.h"
#include <linux/types.h>

/* Filesystem magic number */
#define CODEXFS_MAGIC 114514
#define CODEXFS_SUPERBLK_OFF 0ULL

/* Type definitions matching Rust layout */
typedef u16 codexfs_gid_t;
typedef u16 codexfs_uid_t;
typedef u16 codexfs_mode_t;
typedef u32 codexfs_ino_t;
typedef u64 codexfs_nid_t;
typedef u32 codexfs_blk_t;
typedef u32 codexfs_blk_size_t;
typedef u32 codexfs_blk_off_t;
typedef u64 codexfs_off_t;
typedef u32 codexfs_size_t;

/* CodexFs flags */
#define CODEXFS_COMPRESSED (1 << 0)

#define CODEXFS_NAME_LEN 255

/*
 * On-disk super block structure (128 bytes)
 * Keep layout exactly matching Rust version
 */
struct codexfs_super_block {
	__le32 magic; /* Magic number */
	__le32 checksum; /* CRC32C checksum */
	__u8 blksz_bits; /* Block size in bits */
	__le64 root_nid; /* Root directory nid */
	__le32 inos; /* Total valid inodes */
	__u8 islot_bits; /* Inode slot bits */
	__le32 blocks; /* Total blocks */
	__u8 flags; /* Filesystem flags */
	__u8 reserved[101]; /* Padding to 128 bytes */
} __attribute__((packed));

/* Inode union for different data representations */
union codexfs_inode_union {
	__le16 blks; /* Number of blocks */
	__le32 blk_off; /* Block offset */
};

/* On-disk inode structure (32 bytes) */
struct codexfs_inode {
	__le16 mode; /* File type and mode */
	__le16 nlink; /* Hard links count */
	__le32 size; /* File size in bytes */
	__le32 ino; /* Inode number */
	__le16 uid; /* Owner UID */
	__le16 gid; /* Owner GID */
	__le32 blk_id; /* Starting block ID */
	union codexfs_inode_union u; /* Data representation */
	__u8 reserved[8]; /* Padding */
} __attribute__((packed));

/* Directory entry types */
enum codexfs_file_type {
	CODEXFS_FT_FILE,
	CODEXFS_FT_DIR,
	CODEXFS_FT_CHRDEV,
	CODEXFS_FT_BLKDEV,
	CODEXFS_FT_FIFO,
	CODEXFS_FT_SOCK,
	CODEXFS_FT_SYMLINK,
};

/* Directory entry structure (12 bytes) */
struct codexfs_dirent {
	__le64 nid; /* Node ID */
	__le16 nameoff; /* Name offset in block */
	__u8 file_type; /* File type */
	__u8 reserved; /* Padding */
} __attribute__((packed));

/* Extent structure (8 bytes) */
struct codexfs_extent {
	__le32 off; /* File offset */
	__le32 frag_off; /* Fragment offset */
} __attribute__((packed));

/* check the CODEXFS on-disk layout strictly at compile time */
static inline void codexfs_check_ondisk_layout_definitions(void)
{
	BUILD_BUG_ON(sizeof(struct codexfs_super_block) != 128);
	BUILD_BUG_ON(sizeof(struct codexfs_inode) != 32);
	BUILD_BUG_ON(sizeof(struct codexfs_dirent) != 12);
	BUILD_BUG_ON(sizeof(struct codexfs_extent) != 8);
}

#endif /* __CODEXFS_H */
