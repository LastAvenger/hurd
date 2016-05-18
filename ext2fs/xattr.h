/*
 * Ext2 support for extended attributes
 *
 * Copyright 2006 Thadeu Lima de Souza Cascardo
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef EXT2_XATTR_H
#define EXT2_XATTR_H

/* Identifies whether a block is a proper xattr block. */
#define EXT2_XATTR_BLOCK_MAGIC 0xEA020000

/* xattr block header. */
struct _ext2_xattr_header
{
  int magic;	/* magic number for identification */
  int refcount;		/* reference count */
  int blocks;	/* number of disk blocks used */
  int hash;		/* hash value of all attributes */
  int reserved[4];	/* zero right now */
};

/* xattr entry in xattr block. */
struct _ext2_xattr_entry
{
  char name_len;	/* length of name */
  char name_index;	/* attribute name index */
  short value_offset;	/* offset in disk block of value */
  int value_block;	/* disk block attribute is stored on (n/i) */
  int value_size;	/* size of attribute value */
  int hash;		/* hash value of name and value */
  char name[0];	/* attribute name */
};

typedef struct _ext2_xattr_header ext2_xattr_header;
typedef struct _ext2_xattr_entry ext2_xattr_entry;

#define EXT2_XATTR_PAD 4
#define EXT2_XATTR_ROUND (EXT2_XATTR_PAD - 1)

/* Entry alignment in xattr block. */
#define EXT2_XATTR_ALIGN(x) (((unsigned long) (x) + \
			      EXT2_XATTR_ROUND) & \
			     (~EXT2_XATTR_ROUND))

/* Given a fs block, return the xattr header. */
#define EXT2_XATTR_HEADER(block) ((ext2_xattr_header *) block)

/* Aligned size of entry, including the name length. */
#define EXT2_XATTR_ENTRY_SIZE(len) EXT2_XATTR_ALIGN ((sizeof \
						      (ext2_xattr_entry) + \
						      len))

/* Offset of entry, given the block header. */
#define EXT2_XATTR_ENTRY_OFFSET(header, entry) ((off_t) ((char *) entry - \
							 (char *) header))

/* First entry of xattr block, given its header. */
#define EXT2_XATTR_ENTRY_FIRST(header) ((ext2_xattr_entry *) (header + 1))

/* Next entry, giving an entry. */
#define EXT2_XATTR_ENTRY_NEXT(entry) ((ext2_xattr_entry *) \
				      ((char *) entry + \
				       EXT2_XATTR_ENTRY_SIZE \
				       (entry->name_len)))

/* Checks if this entry is the last (not valid) one. */
#define EXT2_XATTR_ENTRY_LAST(entry) (*(unsigned long *) entry == 0UL)

/* Public functions. */
error_t diskfs_list_xattr (struct node *, char **, int *);
error_t diskfs_get_xattr (struct node *, char *, char *, int *);
error_t diskfs_set_xattr (struct node *, char *, char *, int, int);


#endif
