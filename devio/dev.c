/* Mach devices.

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <hurd.h>
#include <device/device.h>
#include <assert.h>
#include <hurd/pager.h>

#include "dev.h"
#include "iostate.h"

/* ---------------------------------------------------------------- */

/* Returns a pointer to a new device structure in DEV for the kernel device
   NAME, with the given FLAGS.  If BLOCK_SIZE is non-zero, it should be the
   desired block size, and must be a multiple of the device block size.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t
dev_open(char *name, int flags, int block_size, struct dev **dev)
{
  error_t err = 0;
  static mach_port_t device_master = MACH_PORT_NULL;

  *dev = malloc(sizeof (struct dev));

  if (!(flags & DEV_SERIAL))
    flags |= DEV_SEEKABLE;

  if (*dev == NULL)
    return ENOMEM;

  (*dev)->name = malloc (strlen (name) + 1);
  if ((*dev)->name)
    strcpy ((*dev)->name, name);
  else
    err = ENOMEM;

  (*dev)->port = MACH_PORT_NULL;

  if (!err && device_master == MACH_PORT_NULL)
    err = get_privileged_ports(NULL, &device_master);

  if (!err)
    err = device_open(device_master,
		      D_READ | (flags & DEV_READONLY ? D_WRITE : 0),
		      name, &(*dev)->port);

  if (!err)
    {
      int count = DEV_GET_SIZE_COUNT;
      int sizes[DEV_GET_SIZE_COUNT];

      /* Get info about the device: total size (in bytes) and block size (in
	 bytes).  Block size is unit in which device addressing is done.  */
      err = device_get_status((*dev)->port, DEV_GET_SIZE, sizes, &count);
      if (!err)
	{
	  (*dev)->size = sizes[DEV_GET_SIZE_DEVICE_SIZE];
	  (*dev)->dev_block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];
	}
    }

  if (!err)
    {
      if (block_size > 0)
	if (block_size > (*dev)->dev_block_size
	    && block_size % (*dev)->dev_block_size == 0)
	  (*dev)->block_size = block_size;
	else
	  err = EINVAL;
      else
	(*dev)->block_size = (*dev)->dev_block_size;

      if ((*dev)->dev_block_size == 1)
	flags |= DEV_SERIAL;

      (*dev)->flags = flags;
      (*dev)->owner = 0;
    }

  if (!err)
    err = io_state_init(&(*dev)->io_state, *dev);

  if (err)
    {
      if ((*dev)->port != MACH_PORT_NULL)
	mach_port_deallocate(mach_task_self(), (*dev)->port);
      free(*dev);
    }

  return err;
}

/* Free DEV and any resources it consumes.  */
void 
dev_close(struct dev *dev)
{
  if (!dev_is(dev, DEV_READONLY))
    {
      if (dev->pager != NULL)
	pager_shutdown(dev->pager);
      io_state_sync(&dev->io_state, dev);
    }

  device_close(dev->port);
  io_state_finalize(&dev->io_state);

  free(dev);
}

/* ---------------------------------------------------------------- */

/* Try and write out any pending writes to DEV.  If WAIT is true, will wait
   for any paging activity to cease.  */
error_t
dev_sync(struct dev *dev, int wait)
{
  error_t err = 0;

  if (!dev_is(dev, DEV_READONLY))
    {
      struct io_state *ios = &dev->io_state;

      io_state_lock(ios);

      /* Sync any paged backing store.  */
      if (dev->pager != NULL)
	pager_sync(dev->pager, wait);

      /* Write out any stuff buffered in our io_state.  */
      err = io_state_sync(ios, dev);

      io_state_unlock(ios);
    }

  return err;
}

/* ---------------------------------------------------------------- */

/* Writes AMOUNT bytes from the buffer pointed to by BUF to the device DEV.
   *OFFS is incremented to reflect the amount read/written.  Both AMOUNT and
   *OFFS must be multiples of DEV's block size, and either BUF must be
   page-aligned, or dev_write_valid() must return true for these arguments.
   If an error occurs, the error code is returned, otherwise 0.  */
error_t
dev_write(struct dev *dev,
	  vm_address_t buf, vm_size_t amount, vm_offset_t *offs)
{
  int bsize = dev->dev_block_size;
  vm_offset_t written = 0;
  vm_offset_t block = (bsize == 1 ? *offs : *offs / bsize);
  error_t err;

  assert(dev_write_valid(dev, buf, amount, *offs));
  assert(*offs % bsize == 0);
  assert(amount % bsize == 0);

  if (amount < IO_INBAND_MAX)
    err =
      device_write_inband (dev->port, 0, block,
			   (io_buf_ptr_t)buf, amount, &written);
  else
    err =
      device_write (dev->port, 0, block, (io_buf_ptr_t)buf, amount, &written);

  if (!err)
    *offs += written;

  return err;
}

/* Reads AMOUNT bytes from DEV and returns it in BUF and BUF_LEN (using the
   standard mach out-array convention).  *OFFS is incremented to reflect the
   amount read/written.  Both LEN and *OFFS must be multiples of DEV's block
   size.  If an error occurs, the error code is returned, otherwise 0.  */
error_t
dev_read(struct dev *dev,
	 vm_address_t *buf, vm_size_t *buf_len, vm_size_t amount,
	 vm_offset_t *offs)
{
  error_t err = 0;
  int bsize = dev->dev_block_size;
  vm_offset_t read = 0;
  vm_offset_t block = (bsize == 1 ? *offs : *offs / bsize);

  assert(*offs % bsize == 0);
  assert(amount % bsize == 0);

  if (amount < IO_INBAND_MAX)
    {
      if (*buf_len < amount)
	err = vm_allocate(mach_task_self(), buf, amount, 1);
      if (!err)
	err =
	  device_read_inband(dev->port, 0, block,
			     amount, (io_buf_ptr_t)*buf, &read);
    }
  else
    err =
      device_read(dev->port, 0, block, amount, (io_buf_ptr_t *)buf, &read);

  if (!err)
    {
      *offs += read;
      *buf_len = read;
    }

  return err;
}