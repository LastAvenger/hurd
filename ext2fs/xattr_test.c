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

static void
list_xattr_test (struct node *np, int exp_len, char *exp_buf,
		error_t exp_err)
{

  char buf[256];
  int len = sizeof (buf);

  assert (ext2_list_xattr (np, buf, &len) == exp_err);

  assert (len == exp_len);
  assert (memcmp(buf, exp_buf, len) == 0);

}

static void
get_xattr_test (struct node *np, char *exp_key, char *exp_val,
		int exp_len, error_t exp_err)
{
  char buf[256];
  int len = sizeof (buf);

  memset (buf, 0, sizeof(len));
  assert (ext2_get_xattr (np, exp_key, buf, &len) == exp_err);

  assert (len == exp_len);

  buf[len] = 0;
  assert (strcmp (buf, exp_val) == 0);

}

static void
set_xattr_test (struct node *np, char *exp_key,
		char *exp_val, int exp_len,
		int exp_flag, error_t exp_err)
{
  assert (ext2_set_xattr (np, exp_key, exp_val,
    exp_len, exp_flag) == exp_err);

}

static void
hash_xattr_test (struct node *np, unsigned int hash_arr[], int len)
{

  int i;
  void *block;
  block_t blkno;
  struct ext2_inode *ei;
  struct ext2_xattr_header *header;
  struct ext2_xattr_entry *entry;

  ei = dino_ref (np->cache_id);
  blkno = ei->i_file_acl;

  block = disk_cache_block_ref (blkno);

  header = EXT2_XATTR_HEADER (block);
  assert (header->h_hash == hash_arr[0]);

  entry = EXT2_XATTR_ENTRY_FIRST (header);

  for (i = 1; i < len; i++)
    {
      assert (entry->e_hash== hash_arr[i]);
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  assert (EXT2_XATTR_ENTRY_LAST (entry));

  dino_deref (ei);
  disk_cache_block_deref (block);

}

static void
read_test (struct node *np)
{
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
 */

  // Illegal parameter test start
  int len = 0;
  assert (ext2_list_xattr (np, NULL, NULL) == EINVAL);
  assert (ext2_list_xattr (np, NULL, &len) == 0 );
  assert (len == 26);

  assert (ext2_get_xattr (np, NULL, NULL, &len) == EINVAL);
  assert (ext2_get_xattr (np, "user.key_456", NULL, NULL) == EINVAL);
  assert (ext2_get_xattr (np, "acl", NULL, &len) == EOPNOTSUPP);
  assert (ext2_get_xattr (np, "user.key_456", NULL, &len) == 0);
  assert (len == 7);

  assert (ext2_set_xattr (np, NULL, NULL, 0, 0) == EINVAL);
  assert (ext2_set_xattr (np, "user.key_012", "val_012", block_size + 10, 0) == ERANGE);

  assert (ext2_set_xattr (np, "user.key_123", "val_012",
    sizeof ("val_012") - 1, XATTR_CREATE) == EEXIST);
  assert (ext2_set_xattr (np, "user.key_012", "val_012",
    sizeof ("val_012") - 1, XATTR_REPLACE) == ENODATA);
  assert (ext2_set_xattr (np, "user.key_012", NULL, 0, XATTR_CREATE) == EINVAL);
  assert (ext2_set_xattr (np, "user.key_012", NULL, 0, XATTR_REPLACE) == EINVAL);
  assert (ext2_set_xattr (np, "user.key_012", NULL, 0, 0) == ENODATA);

  // Illegal parameter test end
  list_xattr_test (np, 26, "user.key_123\0user.key_456\0", 0);
  get_xattr_test (np, "user.key_123", "val_123", sizeof ("val_123") - 1, 0);
  get_xattr_test (np, "user.key_456", "val_456", sizeof ("val_456") - 1, 0);

  unsigned int hash_arr[] = {0x43cb502e, 0x6cfa2f34, 0x6cff3cd4};
  hash_xattr_test (np, hash_arr, 3);

}

static void
write_test (struct node *np)
{
/* Image for testing:
 *
 *  dd if=/dev/zero of=$(IMG) bs=4M count=10
 *  mkfs.ext2 -b 4096 $(IMG)
 *  mkdir -p tmp
 *  sudo mount $(IMG) ./tmp
 *  sudo touch ./tmp/test || true
 *  sudo umount ./tmp
 *  rm -rf ./tmp
 */

  set_xattr_test (np, "user.key_123", "val_123", sizeof ("val_123") - 1, XATTR_CREATE, 0);
  set_xattr_test (np, "user.key_456", "val_456", sizeof ("val_456") - 1, XATTR_CREATE, 0);
  read_test (np);

  set_xattr_test (np, "user.key_456", "val_2333333333", sizeof ("val_2333333333") - 1, XATTR_REPLACE, 0);
  list_xattr_test (np, 26, "user.key_123\0user.key_456\0", 0);
  get_xattr_test (np, "user.key_123", "val_123", sizeof ("val_123") - 1, 0);
  get_xattr_test (np, "user.key_456", "val_2333333333", sizeof ("val_2333333333") - 1, 0);

  unsigned int hash_arr[] = {0x43fd5628, 0x6cfa2f34, 0x6cc93ad2};
  hash_xattr_test (np, hash_arr, 3);

  set_xattr_test (np, "user.key_123", "abc", sizeof ("abc") - 1, XATTR_REPLACE, 0);
  get_xattr_test (np, "user.key_123", "abc", sizeof ("abc") - 1, 0);
  set_xattr_test (np, "user.key_456", "def", sizeof ("def") - 1, XATTR_REPLACE, 0);
  get_xattr_test (np, "user.key_456", "def", sizeof ("def") - 1, 0);

  set_xattr_test (np, "user.key_123", "val_123", sizeof ("val_123") - 1, XATTR_REPLACE, 0);
  set_xattr_test (np, "user.key_456", "val_456", sizeof ("val_456") - 1, XATTR_REPLACE, 0);
  read_test (np);

  set_xattr_test (np, "user.key_123", NULL, 0, 0, 0);
  list_xattr_test (np, 13, "user.key_456", 0);
  set_xattr_test (np, "user.key_456", NULL, 0, 0, 0);
  list_xattr_test (np, 0, "", 0);

  set_xattr_test (np, "gnu.translotor", "/hurd/hello\0-c\0hello\0",
          sizeof("/hurd/hello\0-c\0hello\0") - 1, XATTR_CREATE, 0);
  list_xattr_test (np, sizeof("gnu.translotor"), "gnu.translotor", 0);
  get_xattr_test (np, "gnu.translotor", "/hurd/hello\0-c\0hello\0",
          sizeof ("/hurd/hello\0-c\0hello\0") - 1, 0);
}

error_t
diskfs_xattr_test (struct node *np)
{
  write_test (np);

  return 0;

}
