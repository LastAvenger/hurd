/* libdiskfs implementation of fs.defs: dir_rename
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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

#include "priv.h"
#include "fs_S.h"

/* To avoid races in checkpath, and to prevent a directory from being
   simultaneously renamed by two processes, we serialize all renames of
   directores with this lock */
static struct mutex renamedirlock = MUTEX_INITIALIZER;
static int renamedirinit;

/* Implement dir_rename as described in <hurd/fs.defs>. */
error_t
diskfs_S_dir_rename (struct protid *fromcred,
		     char *fromname,
		     struct protid *tocred,
		     char *toname)
{
  struct node *fdp, *tdp, *fnp, *tnp, *tmpnp;
  error_t err;
  int isdir;
  struct dirstat *ds = alloca (diskfs_dirstat_size);
  
  if (!fromcred)
    return EOPNOTSUPP;

  /* Verify that tocred really is a port to us XXX */
  if (!tocred)
    return EXDEV;

  if (readonly)
    return EROFS;

  fdp = fromcred->po->np;
  tdp = tocred->po->np;

 tryagain:
  /* Acquire the source; hold a reference to it.  This 
     will prevent anyone from deleting it before we create
     the new link. */
  mutex_lock (&fdp->lock);
  err = diskfs_lookup (fdp, fromname, LOOKUP, &fnp, 0, fromcred);
  mutex_unlock (&fdp->lock);
  if (err)
    return err;

  if (S_ISDIR (fnp->dn_stat.st_mode))
    {
      mutex_unlock (&fnp->lock);
      if (!mutex_try_lock (&renamedirlock))
	{
	  diskfs_nrele (fnp);
	  mutex_lock (&renamedirlock);
	  goto try_again;
	}
      err = diskfs_rename_dir (fdp, fnp, fromname, tdp, toname);
      diskfs_nrele (fnp);
      mutex_unlock (&renamedirlock);
      return err;
    }

  mutex_unlock (&fnp->lock);

  /* We now hold no locks */

  /* Link the node into the new directory. */
  mutex_lock (&tdp->lock);
  
  err = diskfs_lookup (tdp, toname, RENAME, &tnp, ds, tocred);
  if (err && err != ENOENT)
    {
      diskfs_drop_dirstat (ds);
      diskfs_nrele (fnp);
      mutex_unlock (&tdp->lock);
      return err;
    }

  /* rename("foo", "link-to-foo") is guaranteed to return 0 and
     do nothing by Posix. */
  if (tnp == fnp)
    {
      diskfs_drop_dirstat (ds);
      diskfs_nrele (fnp);
      diskfs_nput (tnp);
      mutex_unlock (&tdp->lock);
      return 0;
    }

  /* rename("foo", dir) should fail. */
  if (tnp && S_ISDIR (tnp->dn_stat.st_mode))
    {
      diskfs_drop_dirstat (ds);
      diskfs_nrele (fnp);
      mutex_unlock (&tdp->lock);
      return EISDIR;
    }

  mutex_lock (&fnp->lock);

  /* Increment the link count for the upcoming link */
  if (fnp->dn_stat.st_nlink == LINK_MAX - 1)
    {
      diskfs_drop_dirstat (ds);
      diskfs_nput (fnp);
      mutex_unlock (&tdp->lock);
      return EMLINK;
    }
  fnp->dn_stat.st_nlink++;
  fnp->dn_set_ctime = 1;
  diskfs_node_update (fnp, 1);

  if (tnp)
    {
      err = diskfs_dirrewrite (tdp, fnp, ds);
      if (!err)
	{
	  tnp->dn_stat.st_nlink--;
	  tnp->dn_set_ctime = 1;
	}
      diskfs_nput (tnp);
    }
  else
    err = diskfs_direnter (tdp, toname, fnp, ds, tocred);

  mutex_unlock (&tdp->lock);
  mutex_unlock (&fnp->lock);
  if (err)
    {
      diskfs_nrele (fnp);
      return err;
    }

  /* We now hold no locks */

  /* Now we remove the source.  Unfortunately, we haven't held 
     fdp locked (nor could we), so someone else might have already
     removed it. */
  mutex_lock (&fdp->lock);
  err = diskfs_lookup (fdp, fromname, REMOVE, &tmpnp, ds, fromcred);
  if (err)
    {
      diskfs_drop_dirstat (ds);
      mutex_unlock (&fdp->lock);
      diskfs_nrele (fnp);
      return err;
    }

  if (tmpnp != fnp)
    {
      /* This is no longer the node being renamed, so just return. */
      diskfs_drop_dirstat (ds);
      diskfs_nput (tmpnp);
      diskfs_nrele (fnp);
      mutex_unlock (&fdp->lock);
      return 0;
    }
  
  diskfs_nrele (tmpnp);

  err = diskfs_dirremove (fdp, ds);

  fnp->dn_stat.st_nlink--;
  fnp->dn_set_ctime = 1;
  diskfs_nput (fnp);
  mutex_unlock (&fdp->lock);
  return err;
}
#endif
