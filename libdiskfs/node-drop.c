/* 
   Copyright (C) 1994 Free Software Foundation

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

/* Node NP now has no more references; clean all state.  The
   diskfs_node_refcnt_lock must be held, and will be released
   upon return.  NP must be locked.  */
void
diskfs_drop_node (struct node *np)
{
  mode_t savemode;
  fshelp_kill_translator (&np->translator);
  if (np->dn_stat.st_nlink == 0)
    {
      assert (!diskfs_readonly);

      if (np->allocsize != 0)
	{
	  /* If the node needs to be truncated, then a complication
	     arises, because truncation might require gaining
	     new references to the node.  So, we give ourselves
	     a reference back, unlock the refcnt lock.  Then
	     we are in the state of a normal user, and do the truncate
	     and an nput.  The next time through, this routine
	     will notice that the size is zero, and not have to
	     do anything. */
	  np->references++;
	  spin_unlock (&diskfs_node_refcnt_lock);
	  diskfs_truncate (np, 0);
	  diskfs_nput (np);
	  return;
	}

      savemode = np->dn_stat.st_mode;
      np->dn_stat.st_mode = 0;
      np->dn_stat.st_rdev = 0;
      np->dn_set_ctime = np->dn_set_atime = 1;
      diskfs_node_update (np, 1);
      diskfs_free_node (np, savemode);
    }
  else
    diskfs_node_update (np, 0);

  if (np->dirmod_reqs)
    {
      struct dirmod *dm, *tmp;
      for (dm = np->dirmod_reqs; dm; dm = tmp)
	{
	  mach_port_deallocate (mach_task_self (), dm->port);
	  tmp = dm->next;
	  free (dm);
	}
    }
  if (np->sockaddr)
    mach_port_deallocate (mach_task_self (), np->sockaddr);

  diskfs_node_norefs (np);
  spin_unlock (&diskfs_node_refcnt_lock);
}

      
