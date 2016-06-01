/* xattr test cases, remove me later.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* Enable debug output */
#define EXT2FS_DEBUG
extern int ext2_debug_flag;

#include "ext2fs.h"
#include "xattr.h"
#include <sys/xattr.h>

/* Image for testing:
 *
 *  dd if=/dev/zero of=$(IMG) bs=4M count=10
 *  mkfs.ext2 -b 4096 $(IMG)
 *  mkdir -p tmp
 *  sudo mount $(IMG) ./tmp
 *  sudo touch ./tmp/test || true
 *  sudo setfattr -n user.key_123 -v val_123 ./tmp/test || true
 *  sudo setfattr -n user.key_456 -v val_456 ./tmp/test || true
 *  sudo umount ./tmp
 *  rm -rf ./tmp
 *
 *  ext2fs: (debug) dino_ref: (12) = 0x101d5580
 *  ext2fs: (debug) diskfs_list_xattr: ext2 xattr block found: 1137397806
 *  ext2fs: (debug) xattr_print_entry: entry:
 *  ext2fs: (debug) xattr_print_entry:      ->e_name_len: 7
 *  ext2fs: (debug) xattr_print_entry:      ->e_name_index: 1
 *  ext2fs: (debug) xattr_print_entry:      ->e_value_offs: 4088
 *  ext2fs: (debug) xattr_print_entry:      ->e_value_block: 0
 *  ext2fs: (debug) xattr_print_entry:      ->e_value_size: 7
 *  ext2fs: (debug) xattr_print_entry:      ->e_hash: 1828335412
 *  ext2fs: (debug) xattr_print_entry:      ->e_name: key_123
 *  ext2fs: (debug) xattr_print_entry: entry:
 *  ext2fs: (debug) xattr_print_entry:      ->e_name_len: 7
 *  ext2fs: (debug) xattr_print_entry:      ->e_name_index: 1
 *  ext2fs: (debug) xattr_print_entry:      ->e_value_offs: 4080
 *  ext2fs: (debug) xattr_print_entry:      ->e_value_block: 0
 *  ext2fs: (debug) xattr_print_entry:      ->e_value_size: 7
 *  ext2fs: (debug) xattr_print_entry:      ->e_hash: 1828666580
 *  ext2fs: (debug) xattr_print_entry:      ->e_name: key_456
 *
 */
static void
list_xattr_test (struct node *np)
{
  char buf[256];
  int len = sizeof (buf);
  char *buf_ptr;

  diskfs_list_xattr (np, buf, &len);

  ext2_debug ("%d", len);

  assert (len == 26);
  assert (memcmp(buf, "user.key_123\0user.key_456\0", len) == 0);

  buf_ptr = buf;
  while (len > 0){
      ext2_debug ("%s", buf_ptr);
      len -= strlen (buf_ptr) + 1;
      buf_ptr += strlen (buf_ptr) + 1;
  }

}

static void
get_xattr_test (struct node *np)
{
  char buf[256];
  int len = sizeof (buf);

  memset(buf, 0, sizeof(len));
  diskfs_get_xattr (np, "user.key_123", buf, &len);

  ext2_debug ("len: %d", len);
  assert (len == 7);

  buf[len] = 0;
  ext2_debug("value: %s", buf);
  assert (strcmp(buf, "val_123") == 0);

  memset(buf, 0, sizeof(len));
  diskfs_get_xattr (np, "user.key_456", buf, &len);

  ext2_debug ("len: %d", len);
  assert (len == 7);

  buf[len] = 0;
  ext2_debug("value: %s", buf);
  assert (strcmp(buf, "val_456") == 0);

  void *block;
  struct ext2_inode *ei;
  block_t blkno;
  ext2_xattr_header *header;
  ext2_xattr_entry *entry;

  ei = dino_ref (np->cache_id);
  blkno = ei->i_file_acl;

  block = disk_cache_block_ref (blkno);

  header = EXT2_XATTR_HEADER (block);
  assert (header->h_hash == 1137397806);

  entry = EXT2_XATTR_ENTRY_FIRST (header);
  assert (entry->e_hash== 1828335412);

  entry = EXT2_XATTR_ENTRY_NEXT (entry);
  assert (entry->e_hash== 1828666580);

  entry = EXT2_XATTR_ENTRY_NEXT (entry);
  assert (EXT2_XATTR_ENTRY_LAST (entry));
}

/* Image for testing:
 *
 *  dd if=/dev/zero of=$(IMG) bs=4M count=10
 *  mkfs.ext2 -b 4096 $(IMG)
 *  mkdir -p tmp
 *  sudo mount $(IMG) ./tmp
 *  sudo touch ./tmp/test || true
 *  sudo umount ./tmp
 *  rm -rf ./tmp
 *
 */

static void
set_xattr_test (struct node *np)
{
  char buf[256];
  int len = sizeof (buf);

  /* create */
  diskfs_set_xattr (np, "user.key_123", "val_123",
    sizeof ("val_123") - 1, XATTR_CREATE);
  diskfs_set_xattr (np, "user.key_456", "val_456",
    sizeof ("val_456") - 1, XATTR_CREATE);

  list_xattr_test(np);
  get_xattr_test(np);
}

error_t
diskfs_xattr_test (struct node *np)
{
  list_xattr_test(np);
  get_xattr_test (np);

  // set_xattr_test (np);
  return 0;
}
