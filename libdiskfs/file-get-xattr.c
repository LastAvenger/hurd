/* libdiskfs implementation of fs.defs: file_get_xattr
   Copyright (C) 2016 Free Software Foundation, Inc.

   Written by and Shengyu Zhang <lastavengers@outlook.com>

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

#include "fs_S.h"

/* Implement file_get_xattr as described in <hurd/fs.defs>. */
kern_return_t
diskfs_S_file_get_xattr (struct protid *cred,
			 char *name,
			 char **value,
			 size_t *valuelen)
{
  struct node *np;
  error_t err = 0;

  if (!cred)
    return EOPNOTSUPP;

  np = cred->po->np;

  pthread_mutex_lock (&np->lock);

  if (!S_ISLNK (np->dn_stat.st_mode) ||
    !S_ISREG (np->dn_stat.st_mode) ||
    !S_ISDIR (np->dn_stat.st_mode))
    {
      err = EINVAL;
    }
  else
    {
      size_t len;
      err = diskfs_get_xattr (np, name, NULL, &len);
      if (!err)
	{
	  if (len > *valuelen)
	    *value = mmap (0, len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

	    err = diskfs_get_xattr (np, name, *value, &len);
	    *valuelen = len;
	}
    }

  pthread_mutex_unlock (&np->lock);

  return err;
}
