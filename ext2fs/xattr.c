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

/* Enable debug output */
#define EXT2FS_DEBUG
int ext2_debug_flag = 1;

#include "ext2fs.h"
#include "xattr.h"
#include <stdlib.h>
#include <string.h>

/* FIXME: used for XATTR_CREATE and XATTR_REPLACE. */
#include <sys/xattr.h>

struct _xattr_prefix
{
  int index;
  char *prefix;
  ssize_t size;
};

/* Prefixes are represented as numbers when stored in ext2 filesystems. */
struct _xattr_prefix
xattr_prefixes[] =
{
  {
  1, "user.", sizeof "user." - 1},
  {
  7, "gnu.", sizeof "gnu." - 1},
  {
  0, NULL, 0}
};

/*
 * Given an attribute name in full_name, the ext2 number (index) and
 * suffix name (name) are given.  Returns the index in the array
 * indicating whether a corresponding prefix was found or not.
 */
int
xattr_name_prefix (char *full_name, int *index, char **name)
{
  int i;
  for (i = 0; xattr_prefixes[i].prefix != NULL; i++)
    {
      if (!strncmp (xattr_prefixes[i].prefix, full_name,
		    xattr_prefixes[i].size))
	{
	  *name = full_name + xattr_prefixes[i].size;
	  *index = xattr_prefixes[i].index;
	  break;
	}
    }
  return i;
}

void xattr_print_entry (ext2_xattr_entry *entry)
{
  ext2_debug ("entry:");
  ext2_debug ("\t->e_name_len: %d", entry->e_name_len);
  ext2_debug ("\t->e_name_index: %d", entry->e_name_index);
  ext2_debug ("\t->e_value_offs: %d", entry->e_value_offs);
  ext2_debug ("\t->e_value_block: %d", entry->e_value_block);
  ext2_debug ("\t->e_value_size: %d", entry->e_value_size);
  ext2_debug ("\t->e_hash: %d", entry->e_hash);
  ext2_debug ("\t->e_name: %.*s", entry->e_name_len, entry->e_name);
}

/*
 * Given an entry, appends its name to a buffer.  The provided buffer
 * length is reduced by the name size, even if the buffer is NULL (for
 * computing the list size).  Returns EOPNOTSUPP (operation not
 * supported) if the entry index cannot be found on the array of
 * supported prefixes.  If a buffer is provided (not NULL) and its
 * length is not enough for name, ERANGE is returned.
 * 
 */
error_t
xattr_entry_list (ext2_xattr_entry * entry, char *buffer, int *len)
{

  int i;
  int size;

  for (i = 0; xattr_prefixes[i].prefix != NULL; i++)
    {
      if (entry->e_name_index == xattr_prefixes[i].index)
	break;
    }

  if (xattr_prefixes[i].prefix == NULL)
    return EOPNOTSUPP;

  ext2_debug("prefix: %s, prefix_size: %d", xattr_prefixes[i].prefix,
		  xattr_prefixes[i].size);

  size = xattr_prefixes[i].size + entry->e_name_len + 1;
  ext2_debug("attribute size: %d", size);

  if (buffer)
    {
      if (size <= *len)
	{
	  memcpy (buffer, xattr_prefixes[i].prefix, xattr_prefixes[i].size);
	  buffer += xattr_prefixes[i].size;
	  memcpy (buffer, entry->e_name, entry->e_name_len);
	  buffer += entry->e_name_len;
      *buffer++ = 0;
	}
      else
	{
	  return ERANGE;
	}
    }

  *len -= size;
  return 0;

}

/*
 * Given the xattr block, an entry and a attribute name, retrieves its
 * value. The value length is also returned through parameter len.  In
 * case the name prefix cannot be found in the prefix array,
 * EOPNOTSUPP is returned, indicating the prefix is not supported.  In
 * case there is not enough space in the buffer provided, ERANGE is
 * returned.  If the value buffer was NULL, the length is returned
 * through len parameter and the function is successfull (returns 0).
 * If the entry does not match the name, ENODATA is returned.
 */
error_t
xattr_entry_get (char *block, ext2_xattr_entry * entry, char *full_name,
		 char *value, int *len)
{

  int i;
  int index;
  char *name;

  i = xattr_name_prefix (full_name, &index, &name);

  if (xattr_prefixes[i].prefix == NULL)
    return EOPNOTSUPP;

  if (index != entry->e_name_index
    || strlen (name) != entry->e_name_len
    || strncmp (name, entry->e_name, entry->e_name_len))
    {
      return ENODATA;
    }

  if (value)
    {
      if (*len < entry->e_value_size)
	{
	  return ERANGE;
	}
      memcpy (value, block + entry->e_value_offs, entry->e_value_size);
    }

  *len = entry->e_value_size;
  return 0;

}

/*
 * Creates an entry in the xattr block, giving its header, the last
 * entry, the position where this new one should be inserted, the name
 * of the attribute, its value and the value length, and, the
 * remaining space in the block (parameter rest).  If no space is
 * available for the required size of the entry, ERANGE is returned.
 */
error_t
xattr_entry_create (ext2_xattr_header * header,
		    ext2_xattr_entry * last,
		    ext2_xattr_entry * position,
		    char *name, char *value, int len, int rest)
{

  int name_len;
  off_t start;
  off_t end;
  int entry_size;
  int value_size;
  int index;

  xattr_name_prefix (name, &index, &name);
  // (?) check ret val?
  ext2_debug("name: %s, value: %s, len %d, rest: %d",
	  name, value, len, rest);

  name_len = strlen (name);
  entry_size = EXT2_XATTR_ENTRY_SIZE (name_len);
  value_size = EXT2_XATTR_ALIGN (len);

  if (entry_size + value_size > rest)
    {
      return ERANGE;
    }

  start = EXT2_XATTR_ENTRY_OFFSET (header, position);
  end = EXT2_XATTR_ENTRY_OFFSET (header, last);

  memmove ((char *) position + entry_size, position, end - start);

  position->e_name_len = name_len;
  position->e_name_index = index;
  position->e_value_offs = end + rest - value_size;
  position->e_value_block = 0;
  // (?) why value_block always zero?
  position->e_value_size = len;
  position->e_hash = 0;
  strncpy (position->e_name, name, name_len);
  xattr_print_entry(position);

  memcpy ((char *) header + position->e_value_offs, value, len);
  memset ((char *) header + position->e_value_offs + len, 0,
	  value_size - len);

  return 0;

}

/*
 * Removes an entry from the xattr block, giving a pointer to the
 * block header, the last attribute entry, the position of the entry
 * to be removed and the remaining space in the block.
 */
error_t
xattr_entry_remove (ext2_xattr_header * header,
		    ext2_xattr_entry * last,
		    ext2_xattr_entry * position, int rest)
{

  size_t size;
  off_t start;
  off_t end;
  ext2_xattr_entry *entry;

  /* Remove attribute value */
  size = EXT2_XATTR_ALIGN (position->e_value_size);
  start = EXT2_XATTR_ENTRY_OFFSET (header, last) + rest;
  end = position->e_value_offs;

  memmove ((char *) header + start + size, (char *) header + start,
	   end - start);
  memset ((char *) header + start, 0, end - start);

  entry = EXT2_XATTR_ENTRY_FIRST (header);
  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      if (entry->e_value_offs < end)
	entry->e_value_offs += size;
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  /* Remove attribute name */
  size = EXT2_XATTR_ENTRY_SIZE (position->e_name_len);
  start = EXT2_XATTR_ENTRY_OFFSET (header, position);
  end = EXT2_XATTR_ENTRY_OFFSET (header, last) + rest;

  memmove ((char *) header + start , (char *) header + start + size,
	   end - (start + size));

  return 0;

}

/*
 * Replaces the value of an existing attribute entry, given the block
 * header, the last entry, the entry whose value should be replaced,
 * the new value, its length, and the remaining space in the block.
 * Returns ERANGE if there is not enough space (when the new value is
 * bigger than the old one).
 */
error_t
xattr_entry_replace (ext2_xattr_header * header,
		     ext2_xattr_entry * last,
		     ext2_xattr_entry * position,
		     char *value, int len, int rest)
{

  ssize_t old_size;
  ssize_t new_size;

  old_size = EXT2_XATTR_ALIGN (position->e_value_size);
  new_size = EXT2_XATTR_ALIGN (len);

  if (new_size - old_size > rest)
    return ERANGE;

  if (new_size != old_size)
    {

      off_t start;
      off_t end;
      ext2_xattr_entry *entry;

      start = EXT2_XATTR_ENTRY_OFFSET (header, last) + rest;
      end = position->e_value_offs;

      memmove ((char *) header + start + old_size, (char *) header + start,
	       end - start);

      entry = EXT2_XATTR_ENTRY_FIRST (header);
      while (!EXT2_XATTR_ENTRY_LAST (entry))
	{
	  if (entry->e_value_offs < end)
	    entry->e_value_offs += old_size;
	  entry = EXT2_XATTR_ENTRY_NEXT (entry);
	}

      position->e_value_offs = start - (new_size - old_size);
      position->e_value_size = len;

    }
  else
    {
      position->e_value_size = len;
    }

  memcpy ((char *) header + position->e_value_offs, value, len);
  memset ((char *) header + position->e_value_offs + len, 0, new_size - len);

  return 0;

}


/*
 * Given a node, return its list of attribute names in a buffer.
 * Returns EOPNOTSUPP if underlying filesystem has no extended
 * attributes support.  Returns EIO if xattr block is invalid (has no
 * valid h_magic number).
 */
error_t
diskfs_list_xattr (struct node *np, char *buffer, int *len)
{

  block_t blkno;
  void *block;
  struct ext2_inode *ei;
  ext2_xattr_header *header;
  ext2_xattr_entry *entry;

  int size = *len;

  /* FIXME: macro EXT2_HAS_COMPAT_FEATURE not found */
  /*
  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      // FIXME: remove warning
      ext2_warning ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }
   */

  ei = dino_ref (np->cache_id);

  blkno = ei->i_file_acl;
  ext2_debug("blkno = %d", blkno);

  if (blkno == 0)
    {
      *len = 0;
      return 0;
    }

  block = disk_cache_block_ref (blkno);

  header = EXT2_XATTR_HEADER (block);
  if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
    {
      ext2_warning ("Invalid extended attribute block.");
      return EIO;
    }
  ext2_debug("ext2 xattr block found");

  entry = EXT2_XATTR_ENTRY_FIRST (header);
  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      xattr_print_entry (entry);
      xattr_entry_list (entry, buffer, &size);
      buffer += strlen(buffer) + 1;
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  *len = *len - size;
  return 0;
}


/*
 * Given a node and an attribute name, returns the value and its
 * length in a buffer.  May return EOPNOTSUPP if underlying filesystem
 * does not support extended attributes or the given name prefix.  If
 * there is no sufficient space in value buffer, returns ERANGE.
 * Returns EIO if xattr block is invalid and ENODATA if there is no
 * such block or no entry in the block matching the name.
 */
error_t
diskfs_get_xattr (struct node *np, char *name, char *value, int *len)
{

  int size;
  int err;
  void *block;
  struct ext2_inode *ei;
  block_t blkno;
  ext2_xattr_header *header;
  ext2_xattr_entry *entry;

  if (len)
    size = *len;
  else
    size = 0;

  /* FIXME: macro EXT2_HAS_COMPAT_FEATURE not found */
  /*
  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      // FIXME: remove warning
      ext2_warning ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }
    */

  ei = dino_ref (np->cache_id);

  blkno = ei->i_file_acl;

  if (blkno == 0)
    {
      return ENODATA;
    }

  block = disk_cache_block_ref (blkno);

  header = EXT2_XATTR_HEADER (block);
  if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
    {
      ext2_warning ("Invalid extended attribute block.");
      return EIO;
    }

  entry = EXT2_XATTR_ENTRY_FIRST (header);

  err = ENODATA;

  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      if ((err = xattr_entry_get (block, entry, name, value, &size))
	  != ENODATA)
	break;
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  if (!err && len)
    *len = size;

  return err;

}

/*
 * Set the value of an attribute giving the node, the attribute name,
 * value, the value length and flags.  If flags is XATTR_CREATE, the
 * attribute is created if no existing matching entry is found.
 * Otherwise, EEXIST is returned.  If flags is XATTR_REPLACE, the
 * attribute value is replaced if an entry is found and ENODATA is
 * returned otherwise.  If no flags are used, the entry is properly
 * created or replaced.  The entry is removed if value is NULL and no
 * flags are used.  In this case, if any flags are used, EINVAL is
 * returned.  If no matching entry is found, ENODATA is returned.
 * EOPNOTSUPP is returned in case extended attributes or the name
 * prefix are not supported.  If there is no space available in the
 * block, ERANGE is returned.
 */
error_t
diskfs_set_xattr (struct node *np, char *name, char *value, int len,
		  int flags)
{

  int err;
  int found;
  int rest;
  void *block;
  block_t blkno;
  struct ext2_inode *ei;
  ext2_xattr_header *header;
  ext2_xattr_entry *entry;
  ext2_xattr_entry *location;

  /* FIXME: macro EXT2_HAS_COMPAT_FEATURE not found */
  /*
  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      FIXME: remove warning
      ext2_warning ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }
    */

  ei = dino_ref (np->cache_id);

  blkno = ei->i_file_acl;
  ext2_debug("blkno: %d", blkno);

  if (blkno == 0)
    {
      /* Allocate and initialize new block */
      block_t goal;
      goal = sblock->s_first_data_block + np->dn->info.i_block_group *
	EXT2_BLOCKS_PER_GROUP (sblock);
      blkno = ext2_new_block (goal, 0, 0, 0);
      block = disk_cache_block_ref (blkno);
      memset (block, 0, block_size);
      header = EXT2_XATTR_HEADER (block);
      header->h_magic = EXT2_XATTR_BLOCK_MAGIC;
      header->h_blocks = 1;
      header->h_refcount = 1;
    }
  else
    {
      block = disk_cache_block_ref (blkno);
      header = EXT2_XATTR_HEADER (block);
      if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
	{
	  ext2_warning ("Invalid extended attribute block.");
	  return EIO;
	}
    }

  entry = EXT2_XATTR_ENTRY_FIRST (header);
  location = NULL;

  rest = block_size;
  ext2_debug("rest: %d", block_size);

  err = ENODATA;
  found = FALSE;

  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      int size;
      err = xattr_entry_get (NULL, entry, name, NULL, &size);
      if (err == 0)
	{
	  location = entry;
	  found = TRUE;
	}
      else if (err == ENODATA)
	{
	  location = entry;
	  found = FALSE;
	}
      else
	{
	  break;
	}
      rest -= EXT2_XATTR_ALIGN (entry->e_value_size);
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  if (err != 0 && err != ENODATA)
    return err;

  if (location == NULL)
    location = entry;

  rest -= EXT2_XATTR_ENTRY_OFFSET (header, entry);

  if (rest < 0)
    return EIO;

  if (value && flags & XATTR_CREATE)
    {
      if (found)
	return EEXIST;
      else
	err =
	  xattr_entry_create (header, entry, location, name, value, len,
			      rest);
    }
  else if (value && flags & XATTR_REPLACE)
    {
      if (!found)
	return ENODATA;
      else
	err = xattr_entry_replace (header, entry, location, value, len, rest);
    }
  else if (value)
    {
      if (found)
	err = xattr_entry_replace (header, entry, location, value, len, rest);
      else
	err =
	  xattr_entry_create (header, entry, location, name, value, len,
			      rest);
    }
  else
    {
      if (flags & XATTR_REPLACE || flags & XATTR_CREATE)
	return EINVAL;
      else if (!found)
	return ENODATA;
      else
	err = xattr_entry_remove (header, entry, location, rest);
    }

  if (err == 0)
    {
      record_global_poke (block);
      if (ei->i_file_acl == 0)
	{
	  ei->i_file_acl = blkno;
	  record_global_poke (ei);
	}
    }

  return err;

}

error_t
diskfs_xattr_test (struct node *np)
{
  int len = 32;
  char buf[32];
  // char *buf_ptr;

  /*
  diskfs_list_xattr (np, buf, &len);

  ext2_debug ("len: %d", len);
  buf_ptr = buf;
  while (len > 0){
      ext2_debug ("%s", buf_ptr);
      len -= strlen (buf_ptr) + 1;
      buf_ptr += strlen (buf_ptr) + 1;
  }
  */

  /*
  len = 32;
  memset(buf, 0, sizeof(len));
  diskfs_get_xattr (np, "user.key", buf, &len);
  ext2_debug ("len: %d", len);
  buf[len] = 0;
  ext2_debug("value: %s", buf);
  */

  diskfs_set_xattr (np, "user.key3", "value3", sizeof ("value3") - 1, 0);

  len = 32;
  memset(buf, 0, sizeof (len));
  diskfs_get_xattr (np, "user.key3", buf, &len);
  ext2_debug ("len: %d", len);
  buf[len] = 0;
  ext2_debug("value: %s", buf);

  return 0;
}
