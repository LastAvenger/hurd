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


struct trivfs_protid
{
  struct port_info pi;
  uid_t *uids, *gids;
  int nuids, ngids;
  int isroot;
  mach_port_t realnode;		/* restricted permissions */
  void *hook;			/* for user use */
  struct trivfs_peropen *po;
};

struct trivfs_peropen
{
  void *hook;			/* for user use */
  int openmodes;
  int refcnt;
  struct trivfs_control *cntl;
};

struct trivfs_control
{
  struct port_info pi;
  int protidtypes;
  mach_port_t underlying;
  struct pending_open *openshead, *openstail;
};

/* The user must define these variables. */
extern int trivfs_fstype;
extern int trivfs_fsid;

/* Set these if trivfs should allow read, write, 
   or execute of file.    */
extern int trivfs_support_read;
extern int trivfs_support_write;
extern int trivfs_support_exec;

/* Set this some combination of O_READ, O_WRITE, and O_EXEC;
   trivfs will only allow opens of the specified modes.
   (trivfs_support_* is not used to validate opens, only actual
   operations.)  */
extern int trivfs_allow_open;

extern int trivfs_protid_porttypes[];
extern int trivfs_protid_nporttypes;
extern int trivfs_cntl_porttypes[];
extern int trivfs_cntl_nporttypes;

/* The user must define this function.  This should modify a struct 
   stat (as returned from the underlying node) for presentation to
   callers of io_stat.  It is permissable for this function to do
   nothing.  */
void trivfs_modify_stat (struct stat *);

/* If this variable is set, it is called every time an open happens.
   UIDS, GIDS, and FLAGS are from the open; CNTL identifies the
   node being opened.  This call need not check permissions on the underlying
   node.  If the open call should block, then return EWOULDBLOCK.  Other
   errors are immediately reflected to the user.  If O_NONBLOCK 
   is not set in FLAGS and EWOULDBLOCK is returned, then call 
   trivfs_complete_open when all pending open requests for this 
   file can complete. */
error_t (*trivfs_check_open_hook) (struct trivfs_control *cntl,
				   uid_t *uids, u_int nuids,
				   gid_t *gids, u_int ngids,
				   int flags);

/* Call this after *trivfs_check_open_hook returns EWOULDBLOCK when
   FLAGS did not include O_NONBLOCK.  CNTL identifies the node now
   openable.  If MULTI is nonzero, then return all pending opens,
   otherwise, return only one.  ERR is whether this open should
   return an error, and what error to return. */
void trivfs_complete_open (struct trivfs_control *cntl, 
			   int multi, error_t err);

/* If this variable is set, it is called every time a new protid
   structure is created and initialized. */
void (*trivfs_protid_create_hook) (struct trivfs_protid *);

/* If this variable is set, it is called every time a new peropen
   structure is created and initialized. */
void (*trivfs_peropen_create_hook) (struct trivfs_peropen *);

/* If this variable is set, it is called every time a protid structure
   is about to be destroyed. */
void (*trivfs_protid_destroy_hook) (struct trivfs_protid *);

/* If this variable is set, it is called every time a peropen structure
   is about to be destroyed. */
void (*trivfs_peropen_destroy_hook) (struct trivfs_peropen *);

/* Call this to create a new control port and return a receive right
   for it; exactly one send right must be created from the returned
   receive right.  UNDERLYING is the underlying port, such as fsys_startup
   returns as the realnode.  PROTIDTYPE is the ports type to be used
   for ports that refer to this underlying node.  CNTLTYPE is the ports type
   to be used for the control port for this node. */
mach_port_t trivfs_handle_port (mach_port_t underlying, int cntltype,
				int protidtype);

/* Install these as libports cleanroutines for trivfs_protid_porttype
   and trivfs_cntl_porttype respectively. */
void trivfs_clean_protid (void *);
void trivfs_clean_cntl (void *);

/* This demultiplees messages for trivfs ports. */
int trivfs_demuxer (mach_msg_header_t *, mach_msg_header_t *);

/* The user must define this function.  Someone wants the filesystem
   to go away.  FLAGS are from the set FSYS_GOAWAY_*; REALNODE,
   CNTLTYPE, and PROTIDTYPE are as from the trivfs_handle_port
   call which creade this filesystem. */
error_t trivfs_goaway (int flags, mach_port_t realnode, int cntltype,
		       int protidtype);

/* Call this to set atime for the node to the current time.  */
error_t trivfs_set_atime (struct trivfs_control *cntl);

/* Call this to set mtime for the node to the current time. */
error_t trivfs_set_mtime (struct trivfs_control *cntl);
