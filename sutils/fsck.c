/* Hurd-aware fsck wrapper

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* This wrapper runs other file-system specific fsck programs.  They are
   expected to accept at least the following options:

     -p  Terse automatic mode
     -y  Automatically answer yes to all questions
     -n  Automatically answer no to all questions
     -f  Check even if clean
     -s  Only print diagostic messages
   
   They should also return exit-status codes as following:

     0   Filesystem was clean
     1,2 Filesystem fixed (and is now clean)
     4,8 Filesystem was broken, but couldn't be fixed
     ... Anything else is assumed be some horrible error

   The exit-status from this wrapper is the greatest status returned from any
   individual fsck.

   Although it knows something about the hurd, this fsck still uses
   /etc/fstab, and is generally not very integrated.  That will have to wait
   until the appropiate mechanisms for doing so are decided.  */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <error.h>
#include <argp.h>
#include <argz.h>
#include <assert.h>

#include "fstab.h"

#define FSCK_SEARCH_FMTS "/sbin/fsck.%s"

/* Exit codes we return.  */
#define FSCK_EX_OK       0      /* No errors */
#define FSCK_EX_FIXED    1      /* File system errors corrected */
#define FSCK_EX_BROKEN   4      /* File system errors left uncorrected */
#define FSCK_EX_QUIT	 12	/* Got SIGQUIT */
#define FSCK_EX_SIGNAL	 20	/* Signalled (not SIGQUIT) */
#define FSCK_EX_ERROR	 50
#define FSCK_EX_EXEC	 99	/* Exec failed */
/* Everything else is some sort of fsck problem.  */

/* Things we know about what child fsck's might return.  */
#define FSCK_EX_IS_FIXED(st) ({ int _st = (st); _st >= 1 || _st <= 2; })
#define FSCK_EX_IS_BROKEN(st) ({ int _st = (st); _st >= 4 || _st <= 8; })

/* Common fsck flags.  */
#define FSCK_F_PREEN	0x1
#define FSCK_F_YES	0x2
#define FSCK_F_NO	0x4
#define FSCK_F_FORCE	0x8
#define FSCK_F_SILENT	0x10
#define FSCK_F_VERBOSE	0x20	/* Not passed down.  */

static int got_sigquit = 0, got_sigint = 0;

static void sigquit ()
{
  got_sigquit = 1;
}

static void sigint ()
{
  got_sigint = 1;
}

struct fsck
{
  struct fs *fs;		/* Filesystem being fscked.  */
  int pid;			/* Pid for process.  */
  int was_readonly :1;		/* The fs was readonly before we made it so. */
  struct fsck *next, **self;
};

struct fscks
{
  struct fsck *running;		/* Fsck processes now running.  */
  int free_slots;		/* Number of fsck processes we can start.  */
  int flags;
};

/* Starts FS's fsck program on FS's device, returning the pid of the process.
   If an error is encountered, prints an error message and returns 0.
   Filesystems that need not be fscked at all also return 0 (but don't print
   an error message).  */
static pid_t
fs_start_fsck (struct fs *fs, int flags)
{
  pid_t pid;
  char flags_buf[10];
  char *argv[4], **argp = argv;
  struct fstype *type;
  error_t err = fs_type (fs, &type);

  assert_perror (err);		/* Should already have been checked for. */
  assert (type->program);

  *argp++ = type->program;

  if (flags & ~FSCK_F_VERBOSE)
    {
      char *p = flags_buf;
      *argp++ = flags_buf;
      *p++ = '-';
      if (flags & FSCK_F_PREEN)  *p++ = 'p';
      if (flags & FSCK_F_YES)    *p++ = 'y';
      if (flags & FSCK_F_NO)     *p++ = 'n';
      if (flags & FSCK_F_FORCE)  *p++ = 'f';
      if (flags & FSCK_F_SILENT) *p++ = 's';
      *p = '\0';
    }

  *argp++ = fs->mntent.mnt_fsname;
  *argp = 0;

  if (flags & FSCK_F_VERBOSE)
    {
      char *argz;
      size_t argz_len;
      argz_create (argv, &argz, &argz_len);
      argz_stringify (argz, argz_len, ' ');
      puts (argz);
      free (argz);
    }

  pid = fork ();
  if (pid < 0)
    {
      error (0, errno, "fork");
      return 0;
    }

  if (pid == 0)
    /* Child.  */
    {
      execv (type->program, argv);
      exit (FSCK_EX_EXEC);	/* Exec failed. */
    }

  return pid;
}

/* Start a fsck process for FS running, and add an entry for it to FSCKS.
   This also ensures that if FS is currently mounted, it will be made
   readonly first.  If the fsck is successfully started, 0 is returned,
   otherwise FSCK_EX_ERROR.  */
static int
fscks_start_fsck (struct fscks *fscks, struct fs *fs)
{
  error_t err;
  int mounted, was_readonly;
  struct fsck *fsck;

  if (got_sigint)
    /* We got SIGINT, so we pretend that all fscks got a signal without even
       attempting to run them.  */
    return FSCK_EX_SIGNAL;

#define CK(err, fmt, args...) \
    do { if (err) { error (0, err, fmt , ##args); return FSCK_EX_ERROR; } } while (0)

  err = fs_mounted (fs, &mounted);
  CK (err, "%s: Cannot check mounted state", fs->mntent.mnt_dir);
  
  if (mounted)
    {
      err = fs_readonly (fs, &was_readonly);
      CK (err, "%s: Cannot check readonly state", fs->mntent.mnt_dir);
      if (! was_readonly)
	{
	  err = fs_set_readonly (fs, 1);
	  CK (err, "%s: Cannot make readonly", fs->mntent.mnt_dir);
	}
    }

#undef CK

  /* Ok, any mounted filesystem is safely readonly.  */

  fsck = malloc (sizeof (struct fsck));
  if (! fsck)
    {
      error (0, ENOMEM, "malloc");
      return FSCK_EX_ERROR;
    }

  fsck->fs = fs;
  fsck->was_readonly = was_readonly;
  fsck->next = fscks->running;
  if (fsck->next)
    fsck->next->self = &fsck->next;
  fsck->self = &fscks->running;
  fsck->pid = fs_start_fsck (fs, fscks->flags);
  fscks->running = fsck;

  if (fsck->pid)
    fscks->free_slots--;

  return 0;
}

/* Cleanup after fscking with FSCK.  If REMOUNT is true, ask the filesystem
   to remount itself (to incorporate changes made by the fsck program).  If
   RESTORE_WRITABLE is true, then if the filesystem was mounted writable
   prior to fscking, make it writable once more (after remounting if
   applicable).  */
static void
fsck_cleanup (struct fsck *fsck, int remount, int restore_writable)
{
  error_t err = 0;
  struct fs *fs = fsck->fs;

  *fsck->self = fsck->next;	/* Remove from chain.  */

  if (fs->mounted > 0)
    /* It's currently mounted; if the fsck modified the device, tell the
       running filesystem to remount it.  Also we may make it writable.  */
    {
      if (remount)
	{
	  err = fs_remount (fs);
	  if (err)
	    error (0, err, "%s: Cannot remount", fs->mntent.mnt_dir);
	}
      if (!err && !fsck->was_readonly && restore_writable)
	{
	  err = fs_set_readonly (fs, 0);
	  if (err)
	    error (0, err, "%s: Cannot make writable", fs->mntent.mnt_dir);
	}
    }

   free (fsck);
}

/* Wait for some fsck process to exit, cleaning up after it, and return its
   exit-status.  */
static int
fscks_wait (struct fscks *fscks)
{
  pid_t pid;
  int wstatus, status;
  struct fsck *fsck, *next;

  /* Cleanup fscks that didn't even start.  */
  for (fsck = fscks->running; fsck; fsck = next)
    {
      next = fsck->next;
      if (fsck->pid == 0)
	fsck_cleanup (fsck, 0, 1);
    }

  do 
    pid = wait (&wstatus);
  while (pid < 0 && errno == EINTR);

  if (pid > 0)
    {
      if (WIFEXITED (wstatus))
	status = WEXITSTATUS (wstatus);
      else if (WIFSIGNALED (wstatus))
	status = FSCK_EX_SIGNAL;
      else
	status = FSCK_EX_ERROR;

      for (fsck = fscks->running; fsck; fsck = fsck->next)
	if (fsck->pid == pid)
	  {
	    int remount = (status != 0);
	    int restore_writable = (status == 0 || FSCK_EX_IS_FIXED (status));
	    fsck_cleanup (fsck, remount, restore_writable);
	    fscks->free_slots++;
	    break;
	  }
      if (! fsck)
	error (0, 0, "%d: Unknown process exited", pid);
    }
  else if (errno == ECHILD)
    /* There are apparently no child processes left, and we weren't told of
       their demise.  This can't happen.  */
    {
      while (fscks->running)
	{
	  error (0, 0, "%s: Fsck process disappeared!",
		 fscks->running->fs->mntent.mnt_fsname);
	  /* Be pessimistic -- remount the filesystem, but leave it
	     readonly.  */
	  fsck_cleanup (fscks->running, 1, 0);
	  fscks->free_slots++;
	}
      status = FSCK_EX_ERROR;
    }
  else
    status = FSCK_EX_ERROR;	/* What happened?  */

  return status;
}

/* Fsck all the filesystems in FSTAB, with the flags in FLAGS, doing at most
   MAX_PARALLEL parallel fscks.  The greatest exit code returned by any one
   fsck is returned.  */
static int
fsck (struct fstab *fstab, int flags, int max_parallel)
{
  int pass;
  struct fs *fs;
  int summary_status = 0;
  struct fscks *fscks = malloc (sizeof (struct fscks));

  void merge_status (int status)
    {
      if (status > summary_status)
	summary_status = status;
    }

  if (! fscks)
    error (FSCK_EX_ERROR, ENOMEM, "malloc");
  fscks->running = 0;
  fscks->flags = flags;
  fscks->free_slots = max_parallel;

  /* Do in pass order (pass 0 is skipped).  */
  for (pass = 1; pass >= 0; pass = fstab_next_pass (fstab, pass))
    /* Submit all filesystems in the given pass, up to MAX_PARALLEL at a
       time. */
    for (fs = fstab->entries; fs; fs = fs->next)
      if (fs->mntent.mnt_passno == pass)
	/* FS is applicable for this pass.  */
	{
	  struct fstype *type;
	  error_t err = fs_type (fs, &type);

	  if (err)
	    {
	      error (0, err, "%s: Cannot find fsck program (type %s)",
		     fs->mntent.mnt_dir, fs->mntent.mnt_type);
	      merge_status (FSCK_EX_ERROR);
	    }
	  else if (type->program)
	    /* This is a fsckable filesystem.  */
	    {
	      while (fscks->free_slots == 0)
		/* No room; wait for another fsck to finish.  */
		merge_status (fscks_wait (fscks));
	      merge_status (fscks_start_fsck (fscks, fs));
	    }
	}

  free (fscks);

  return summary_status;
}

static const struct argp_option
options[] =
{
  {"preen",      'p', 0,      0, "Terse automatic mode", 1},
  {"yes",        'y', 0,      0, "Automatically answer yes to all questions"},
  {"no",         'n', 0,      0, "Automatically answer no to all questions"},
  {"fstab",	 't', "FILE", 0, "File to use instead of " _PATH_MNTTAB},
  {"parallel",   'l', "NUM",  0, "Limit the number of parallel checks to NUM"},
  {"verbose",	 'v', 0,      0, "Print informational messages"},
  {"search-fmts",'S', "FMTS", 0,
     "`:' separated list of formats to use for finding fsck programs"},
  {0, 0, 0, 0, "In --preen mode, the following also apply:", 2},
  {"force",	 'f', 0,      0, "Check even if clean"},
  {"silent",     's', 0,      0, "Only print diagostic messages"},
  {0, 0}
};
static const char *args_doc = "DEVICE";
static const char *doc = 0;

int
main (int argc, char **argv)
{
  error_t err;
  struct fstab *fstab, *check;
  struct fstypes *types;
  int flags = 0;
  char *names = 0;
  size_t names_len = 0;
  char *search_fmts = FSCK_SEARCH_FMTS;
  size_t search_fmts_len = sizeof FSCK_SEARCH_FMTS;
  char *fstab_path = _PATH_MNTTAB;
  int max_parallel = -1;

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'p': flags |= FSCK_F_PREEN; break;
	case 'y': flags |= FSCK_F_YES; break;
	case 'n': flags |= FSCK_F_NO; break;
	case 'f': flags |= FSCK_F_FORCE; break;
	case 's': flags |= FSCK_F_SILENT; break;
	case 'v': flags |= FSCK_F_VERBOSE; break;
	case 't': fstab_path = arg; break; 
	case 'l':
	  max_parallel = atoi (arg);
	  if (! max_parallel)
	    argp_error (state, "%s: Invalid value for --max-parellel", arg);
	  break;
	case 'S':
	  argz_create_sep (arg, ':', &search_fmts, &search_fmts_len);
	  break;
	case ARGP_KEY_ARG:
	  err = argz_add (&names, &names_len, arg);
	  if (err)
	    argp_failure (state, 100, ENOMEM, "%s", arg);
	  break;
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0, 0);

  err = fstypes_create (search_fmts, search_fmts_len, &types);
  if (err)
    error (102, err, "fstypes_create");

  err = fstab_create (types, &fstab);
  if (err)
    error (101, err, "fstab_create");

  err = fstab_read (fstab, _PATH_MNTTAB);
  if (err)
    error (103, err, "%s", _PATH_MNTTAB);

  if (names)
    /* Fsck specified filesystems; also look at /var/run/mtab.  */
    {
      char *name;

      err = fstab_read (fstab, _PATH_MOUNTED);
      if (err)
	error (104, err, "%s", _PATH_MOUNTED);

      err = fstab_create (types, &check);
      if (err)
	error (105, err, "fstab_create");

      for (name = names; name; name = argz_next (names, names_len, name))
	{
	  struct fs *fs = fstab_find (fstab, name);
	  if (! fs)
	    error (106, 0, "%s: Unknown device or filesystem", name);
	  fstab_add_fs (check, fs, 0);
	}
    }
  else
    /* Fsck everything in /etc/fstab.  */
    check = fstab;

  if (max_parallel <= 0)
    if (flags & FSCK_F_PREEN)
      max_parallel = 100;	/* In preen mode, do lots in parallel.  */
    else
      max_parallel = 1;		/* Do one at a time to keep output rational. */

  /* If the user send a SIGQUIT (usually ^\), then do all checks, but
     regardless of their outcome, return a status that will cause the
     automatic reboot to stop after fscking is complete.  */
  signal (SIGQUIT, sigquit);

  /* Let currently running fscks complete (each such program can handle
     signals as it sees fit), and cause not-yet-run fscks to act as if they
     got a signal.  */
  signal (SIGINT, sigint);

  status = fsck (check, flags, max_parallel);
  if (got_sigquit && status < FSCK_EX_QUIT)
    status = FSCK_EX_QUIT;

  exit (status);
}
