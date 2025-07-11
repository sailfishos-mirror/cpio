/* copyin.c - extract or list a cpio archive
   Copyright (C) 1990-2025 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program.  If not, see
   <http://www.gnu.org/licenses/>. */

#include <system.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "filetypes.h"
#include "cpiohdr.h"
#include "dstring.h"
#include "extern.h"
#include "defer.h"
#include <rmt.h>
#ifndef	FNM_PATHNAME
# include <fnmatch.h>
#endif
#include <hash.h>

#ifndef HAVE_LCHOWN
# define lchown(f,u,g) 0
#endif
#include <timespec.h>

static void copyin_regular_file(struct cpio_file_stat* file_hdr,
				int in_file_des);

void
warn_junk_bytes (long bytes_skipped)
{
  error (0, 0, ngettext ("warning: skipped %ld byte of junk",
			 "warning: skipped %ld bytes of junk", bytes_skipped),
	 bytes_skipped);
}


static int
query_rename(struct cpio_file_stat* file_hdr, FILE *tty_in, FILE *tty_out,
	     FILE *rename_in)
{
  char *str_res;		/* Result for string function.  */
  static dynamic_string new_name;	/* New file name for rename option.  */
  static int initialized_new_name = false;

  if (!initialized_new_name)
    {
      ds_init (&new_name);
      initialized_new_name = true;
    }

  if (rename_flag)
    {
      fprintf (tty_out, _("rename %s -> "), file_hdr->c_name);
      fflush (tty_out);
      str_res = ds_fgets (tty_in, &new_name);
    }
  else
    {
      str_res = ds_fgetstr (rename_in, &new_name, '\n');
    }
  if (str_res == NULL || str_res[0] == 0)
    {
      return -1;
    }
  else
    cpio_set_c_name (file_hdr, new_name.ds_string);
  return 0;
}

/* Skip the padding on IN_FILE_DES after a header or file,
   up to the next header.
   The number of bytes skipped is based on OFFSET -- the current offset
   from the last start of a header (or file) -- and the current
   header type.  */

static void
tape_skip_padding (int in_file_des, off_t offset)
{
  off_t pad;

  if (archive_format == arf_crcascii || archive_format == arf_newascii)
    pad = (4 - (offset % 4)) % 4;
  else if (archive_format == arf_binary || archive_format == arf_hpbinary)
    pad = (2 - (offset % 2)) % 2;
  else if (archive_format == arf_tar || archive_format == arf_ustar)
    pad = (512 - (offset % 512)) % 512;
  else
    pad = 0;

  if (pad != 0)
    tape_toss_input (in_file_des, pad);
}

static char *
get_link_name (struct cpio_file_stat *file_hdr, int in_file_des)
{
  char *link_name;

  if (file_hdr->c_filesize < 0 || file_hdr->c_filesize > SIZE_MAX-1)
    {
      error (0, 0, _("%s: stored filename length is out of range"),
	     file_hdr->c_name);
      link_name = NULL;
    }
  else
    {
      link_name = xmalloc (file_hdr->c_filesize + 1);
      tape_buffered_read (link_name, in_file_des, file_hdr->c_filesize);
      link_name[file_hdr->c_filesize] = '\0';
      tape_skip_padding (in_file_des, file_hdr->c_filesize);
    }
  return link_name;
}

static void
list_file (struct cpio_file_stat* file_hdr, int in_file_des)
{
  if (verbose_flag)
    {
#ifdef CP_IFLNK
      if ((file_hdr->c_mode & CP_IFMT) == CP_IFLNK)
	{
	  if (archive_format != arf_tar && archive_format != arf_ustar)
	    {
	      char *link_name = get_link_name (file_hdr, in_file_des);
	      if (link_name)
		{
		  long_format (file_hdr, link_name);
		  free (link_name);
		}
	    }
	  else
	    long_format (file_hdr, file_hdr->c_tar_linkname);
	  return;
	}
      else
#endif
	long_format (file_hdr, (char *) 0);
    }
  else
    {
      /* Print out the name as it is.  The name_end delimiter is normally
	 '\n', but can be reset to '\0' by the -0 option. */
      printf ("%s%c", file_hdr->c_name, name_end);
    }

  crc = 0;
  tape_toss_input (in_file_des, file_hdr->c_filesize);
  tape_skip_padding (in_file_des, file_hdr->c_filesize);
  if (only_verify_crc_flag)
    {
#ifdef CP_IFLNK
      if ((file_hdr->c_mode & CP_IFMT) == CP_IFLNK)
	{
	  return;   /* links don't have a checksum */
	}
#endif
      if (crc != file_hdr->c_chksum)
	{
	  error (0, 0, _("%s: checksum error (0x%x, should be 0x%x)"),
		 file_hdr->c_name, crc, file_hdr->c_chksum);
	}
    }
}

static int
try_existing_file (struct cpio_file_stat* file_hdr, int in_file_des,
		   bool *existing_dir)
{
  struct stat file_stat;

  *existing_dir = false;
  if (lstat (file_hdr->c_name, &file_stat) == 0)
    {
      if (S_ISDIR (file_stat.st_mode)
	  && ((file_hdr->c_mode & CP_IFMT) == CP_IFDIR))
	{
	  /* If there is already a directory there that
	     we are trying to create, don't complain about
	     it.  */
	  *existing_dir = true;
	  return 0;
	}
      else if (!unconditional_flag
	       && file_hdr->c_mtime <= file_stat.st_mtime)
	{
	  error (0, 0, _("%s not created: newer or same age version exists"),
		 file_hdr->c_name);
	  tape_toss_input (in_file_des, file_hdr->c_filesize);
	  tape_skip_padding (in_file_des, file_hdr->c_filesize);
	  return -1;	/* Go to the next file.  */
	}
      else if (S_ISDIR (file_stat.st_mode)
		? rmdir (file_hdr->c_name)
		: unlink (file_hdr->c_name))
	{
	  error (0, errno, _("cannot remove current %s"),
		 file_hdr->c_name);
	  tape_toss_input (in_file_des, file_hdr->c_filesize);
	  tape_skip_padding (in_file_des, file_hdr->c_filesize);
	  return -1;	/* Go to the next file.  */
	}
    }
  return 0;
}

/* The newc and crc formats store multiply linked copies of the same file
   in the archive only once.  The actual data is attached to the last link
   in the archive, and the other links all have a filesize of 0.  When a
   file in the archive has multiple links and a filesize of 0, its data is
   probably "attatched" to another file in the archive, so we can't create
   it right away.  We have to "defer" creating it until we have created
   the file that has the data "attatched" to it.  We keep a list of the
   "defered" links on deferments.  */

struct deferment *deferments = NULL;

/* Add a file header to the deferments list.  For now they all just
   go on one list, although we could optimize this if necessary.  */

static void
defer_copyin (struct cpio_file_stat *file_hdr)
{
  struct deferment *d;
  d = create_deferment (file_hdr);
  d->next = deferments;
  deferments = d;
  return;
}

/* We just created a file that (probably) has some other links to it
   which have been defered.  Go through all of the links on the deferments
   list and create any which are links to this file.  */

static void
create_defered_links (struct cpio_file_stat *file_hdr)
{
  struct deferment *d;
  struct deferment *d_prev;
  ino_t	ino;
  int	maj;
  int   min;
  int	link_res;
  ino = file_hdr->c_ino;
  maj = file_hdr->c_dev_maj;
  min = file_hdr->c_dev_min;
  d = deferments;
  d_prev = NULL;
  while (d != NULL)
    {
      if ( (d->header.c_ino == ino) && (d->header.c_dev_maj == maj)
	  && (d->header.c_dev_min == min) )
	{
	  struct deferment *d_free;
	  link_res = link_to_name (d->header.c_name, file_hdr->c_name);
	  if (link_res < 0)
	    {
	      error (0, errno, _("cannot link %s to %s"),
		     d->header.c_name, file_hdr->c_name);
	    }
	  if (d_prev != NULL)
	    d_prev->next = d->next;
	  else
	    deferments = d->next;
	  d_free = d;
	  d = d->next;
	  free_deferment (d_free);
	}
      else
	{
	  d_prev = d;
	  d = d->next;
	}
    }
}

/* We are skipping a file but there might be other links to it that we
   did not skip, so we have to copy its data for the other links.  Find
   the first link that we didn't skip and try to create that.  That will
   then create the other deferred links.  */

static int
create_defered_links_to_skipped (struct cpio_file_stat *file_hdr,
				 int in_file_des)
{
  struct deferment *d;
  struct deferment *d_prev;
  ino_t	ino;
  int	maj;
  int   min;
  if (file_hdr->c_filesize == 0)
    {
      /* The file doesn't have any data attached to it so we don't have
	 to bother.  */
      return -1;
    }
  ino = file_hdr->c_ino;
  maj = file_hdr->c_dev_maj;
  min = file_hdr->c_dev_min;
  d = deferments;
  d_prev = NULL;
  while (d != NULL)
    {
      if ( (d->header.c_ino == ino) && (d->header.c_dev_maj == maj)
	  && (d->header.c_dev_min == min) )
	{
	  if (d_prev != NULL)
	    d_prev->next = d->next;
	  else
	    deferments = d->next;
	  cpio_set_c_name (file_hdr, d->header.c_name);
	  free_deferment (d);
	  copyin_regular_file(file_hdr, in_file_des);
	  return 0;
	}
      else
	{
	  d_prev = d;
	  d = d->next;
	}
    }
  return -1;
}

/* If we had a multiply linked file that really was empty then we would
   have defered all of its links, since we never found any with data
   "attached", and they will still be on the deferment list even when
   we are done reading the whole archive.  Write out all of these
   empty links that are still on the deferments list.  */

static void
create_final_defers (void)
{
  struct deferment *d;
  int	link_res;
  int	out_file_des;

  for (d = deferments; d != NULL; d = d->next)
    {
      /* Debian hack: A line, which could cause an endless loop, was
	 removed (97/1/2).  It was reported by Ronald F. Guilmette to
	 the upstream maintainers. -BEM */
      /* Debian hack:  This was reported by Horst Knobloch. This bug has
	 been reported to "bug-gnu-utils@prep.ai.mit.edu". (99/1/6) -BEM
	 */
      link_res = link_to_maj_min_ino (d->header.c_name,
		    d->header.c_dev_maj, d->header.c_dev_min,
		    d->header.c_ino);
      if (link_res == 0)
	{
	  continue;
	}
      out_file_des = open (d->header.c_name,
			   O_CREAT | O_WRONLY | O_BINARY, 0600);
      if (out_file_des < 0 && create_dir_flag)
	{
	  create_all_directories (d->header.c_name);
	  out_file_des = open (d->header.c_name,
			       O_CREAT | O_WRONLY | O_BINARY,
			       0600);
	}
      if (out_file_des < 0)
	{
	  open_error (d->header.c_name);
	  continue;
	}

      set_perms (out_file_des, &d->header);

      if (close (out_file_des) < 0)
	close_error (d->header.c_name);

    }
}

static void
copyin_regular_file (struct cpio_file_stat* file_hdr, int in_file_des)
{
  int out_file_des;		/* Output file descriptor.  */

  if (to_stdout_option)
    out_file_des = STDOUT_FILENO;
  else
    {
      /* Can the current file be linked to a previously copied file? */
      if (file_hdr->c_nlink > 1
	  && (archive_format == arf_newascii
	      || archive_format == arf_crcascii) )
	{
	  int link_res;
	  if (file_hdr->c_filesize == 0)
	    {
	      /* The newc and crc formats store multiply linked copies
		 of the same file in the archive only once.  The
		 actual data is attached to the last link in the
		 archive, and the other links all have a filesize
		 of 0.  Since this file has multiple links and a
		 filesize of 0, its data is probably attatched to
		 another file in the archive.  Save the link, and
		 process it later when we get the actual data.  We
		 can't just create it with length 0 and add the
		 data later, in case the file is readonly.  We still
		 lose if its parent directory is readonly (and we aren't
		 running as root), but there's nothing we can do about
		 that.  */
	      defer_copyin (file_hdr);
	      tape_toss_input (in_file_des, file_hdr->c_filesize);
	      tape_skip_padding (in_file_des, file_hdr->c_filesize);
	      return;
	    }
	  /* If the file has data (filesize != 0), then presumably
	     any other links have already been defer_copyin'ed(),
	     but GNU cpio version 2.0-2.2 didn't do that, so we
	     still have to check for links here (and also in case
	     the archive was created and later appeneded to). */
	  /* Debian hack: (97/1/2) This was reported by Ronald
	     F. Guilmette to the upstream maintainers. -BEM */
	  link_res = link_to_maj_min_ino (file_hdr->c_name,
		    file_hdr->c_dev_maj, file_hdr->c_dev_min,
					  file_hdr->c_ino);
	  if (link_res == 0)
	    {
	      tape_toss_input (in_file_des, file_hdr->c_filesize);
	      tape_skip_padding (in_file_des, file_hdr->c_filesize);
	      return;
	    }
	}
      else if (file_hdr->c_nlink > 1
	       && archive_format != arf_tar
	       && archive_format != arf_ustar)
	{
	  int link_res;
	  /* Debian hack: (97/1/2) This was reported by Ronald
	     F. Guilmette to the upstream maintainers. -BEM */
	  link_res = link_to_maj_min_ino (file_hdr->c_name,
					  file_hdr->c_dev_maj,
					  file_hdr->c_dev_min,
					  file_hdr->c_ino);
	  if (link_res == 0)
	    {
	      tape_toss_input (in_file_des, file_hdr->c_filesize);
	      tape_skip_padding (in_file_des, file_hdr->c_filesize);
	      return;
	    }
	}
      else if ((archive_format == arf_tar || archive_format == arf_ustar)
	       && file_hdr->c_tar_linkname
	       && file_hdr->c_tar_linkname[0] != '\0')
	{
	  int	link_res;
	  link_res = link_to_name (file_hdr->c_name, file_hdr->c_tar_linkname);
	  if (link_res < 0)
	    {
	      error (0, errno, _("cannot link %s to %s"),
		     file_hdr->c_tar_linkname, file_hdr->c_name);
	    }
	  return;
	}

      /* If not linked, copy the contents of the file.  */
      out_file_des = open (file_hdr->c_name,
			   O_CREAT | O_WRONLY | O_BINARY, 0600);

      if (out_file_des < 0 && create_dir_flag)
	{
	  create_all_directories (file_hdr->c_name);
	  out_file_des = open (file_hdr->c_name,
			       O_CREAT | O_WRONLY | O_BINARY,
			       0600);
	}

      if (out_file_des < 0)
	{
	  open_error (file_hdr->c_name);
	  tape_toss_input (in_file_des, file_hdr->c_filesize);
	  tape_skip_padding (in_file_des, file_hdr->c_filesize);
	  return;
	}
    }

  crc = 0;
  if (swap_halfwords_flag)
    {
      if ((file_hdr->c_filesize % 4) == 0)
	swapping_halfwords = true;
      else
	error (0, 0, _("cannot swap halfwords of %s: odd number of halfwords"),
	       file_hdr->c_name);
    }
  if (swap_bytes_flag)
    {
      if ((file_hdr->c_filesize % 2) == 0)
	swapping_bytes = true;
      else
	error (0, 0, _("cannot swap bytes of %s: odd number of bytes"),
	       file_hdr->c_name);
    }
  copy_files_tape_to_disk (in_file_des, out_file_des, file_hdr->c_filesize);
  disk_empty_output_buffer (out_file_des, true);

  if (to_stdout_option)
    {
      if (archive_format == arf_crcascii)
	{
	  if (crc != file_hdr->c_chksum)
	    error (0, 0, _("%s: checksum error (0x%x, should be 0x%x)"),
		   file_hdr->c_name, crc, file_hdr->c_chksum);
	}
      tape_skip_padding (in_file_des, file_hdr->c_filesize);
      return;
    }

  set_perms (out_file_des, file_hdr);

  if (close (out_file_des) < 0)
    close_error (file_hdr->c_name);

  if (archive_format == arf_crcascii)
    {
      if (crc != file_hdr->c_chksum)
	error (0, 0, _("%s: checksum error (0x%x, should be 0x%x)"),
	       file_hdr->c_name, crc, file_hdr->c_chksum);
    }

  tape_skip_padding (in_file_des, file_hdr->c_filesize);
  if (file_hdr->c_nlink > 1
      && (archive_format == arf_newascii || archive_format == arf_crcascii) )
    {
      /* (see comment above for how the newc and crc formats
	 store multiple links).  Now that we have the data
	 for this file, create any other links to it which
	 we defered.  */
      create_defered_links (file_hdr);
    }
}

static void
copyin_device (struct cpio_file_stat* file_hdr)
{
  int res;			/* Result of various function calls.  */

  if (to_stdout_option)
    return;

  if (file_hdr->c_nlink > 1 && archive_format != arf_tar
      && archive_format != arf_ustar)
    {
      int link_res;
      /* Debian hack:  This was reported by Horst
	 Knobloch. This bug has been reported to
	 "bug-gnu-utils@prep.ai.mit.edu". (99/1/6) -BEM */
      link_res = link_to_maj_min_ino (file_hdr->c_name,
		    file_hdr->c_dev_maj, file_hdr->c_dev_min,
		    file_hdr->c_ino);
      if (link_res == 0)
	{
	  return;
	}
    }
  else if (archive_format == arf_ustar &&
	   file_hdr->c_tar_linkname &&
	   file_hdr->c_tar_linkname [0] != '\0')
    {
      int	link_res;
      link_res = link_to_name (file_hdr->c_name,
			       file_hdr->c_tar_linkname);
      if (link_res < 0)
	{
	  error (0, errno, _("cannot link %s to %s"),
		 file_hdr->c_tar_linkname, file_hdr->c_name);
	  /* Something must be wrong, because we couldn't
	     find the file to link to.  But can we assume
	     that the device maj/min numbers are correct
	     and fall through to the mknod?  It's probably
	     safer to just return, rather than possibly
	     creating a bogus device file.  */
	}
      return;
    }

  res = mknod (file_hdr->c_name, file_hdr->c_mode,
	    makedev (file_hdr->c_rdev_maj, file_hdr->c_rdev_min));
  if (res < 0 && create_dir_flag)
    {
      create_all_directories (file_hdr->c_name);
      res = mknod (file_hdr->c_name, file_hdr->c_mode,
	    makedev (file_hdr->c_rdev_maj, file_hdr->c_rdev_min));
    }
  if (res < 0)
    {
      mknod_error (file_hdr->c_name);
      return;
    }
  if (!no_chown_flag)
    {
      uid_t uid = set_owner_flag ? set_owner : file_hdr->c_uid;
      gid_t gid = set_group_flag ? set_group : file_hdr->c_gid;
      if ((chown (file_hdr->c_name, uid, gid) < 0)
	  && errno != EPERM)
	chown_error_details (file_hdr->c_name, uid, gid);
    }
  /* chown may have turned off some permissions we wanted. */
  if (chmod (file_hdr->c_name, file_hdr->c_mode) < 0)
    chmod_error_details (file_hdr->c_name, file_hdr->c_mode);
  if (retain_time_flag)
    set_file_times (-1, file_hdr->c_name, file_hdr->c_mtime,
		    file_hdr->c_mtime, 0);
}

struct delayed_link
  {
    /* The device and inode number of the placeholder. */
    dev_t dev;
    ino_t ino;

    /* The desired link metadata. */
    mode_t mode;
    uid_t uid;
    gid_t gid;
    time_t mtime;

    /* Link source and target names. */
    char *source;
    char target[1];
  };

static Hash_table *delayed_link_table;

static size_t
dl_hash (void const *entry, size_t table_size)
{
  struct delayed_link const *dl = entry;
  uintmax_t n = dl->dev;
  int nshift = (sizeof (n) - sizeof (dl->dev)) * CHAR_BIT;
  if (0 < nshift)
    n <<= nshift;
  n ^= dl->ino;
  return n % table_size;
}

static bool
dl_compare (void const *a, void const *b)
{
  struct delayed_link const *da = a, *db = b;
  return (da->dev == db->dev) & (da->ino == db->ino);
}

static int
symlink_placeholder (char *oldpath, char *newpath, struct cpio_file_stat *file_stat)
{
  int fd = open (newpath, O_WRONLY | O_CREAT | O_EXCL, 0);
  struct stat st;
  struct delayed_link *p;
  size_t newlen = strlen (newpath);

  if (fd < 0 && create_dir_flag)
    {
      create_all_directories (newpath);
      fd = open (newpath, O_WRONLY | O_CREAT | O_EXCL, 0);
    }

  if (fd < 0)
    {
      open_error (newpath);
      return -1;
    }

  if (fstat (fd, &st) != 0)
    {
      stat_error (newpath);
      close (fd);
      return -1;
    }

  close (fd);

  p = xmalloc (sizeof (*p) + strlen (oldpath) + newlen + 1);
  p->dev = st.st_dev;
  p->ino = st.st_ino;

  p->mode = file_stat->c_mode;
  p->uid = file_stat->c_uid;
  p->gid = file_stat->c_gid;
  p->mtime = file_stat->c_mtime;

  strcpy (p->target, newpath);
  p->source = p->target + newlen + 1;
  strcpy (p->source, oldpath);

  if (!((delayed_link_table
	 || (delayed_link_table = hash_initialize (0, 0, dl_hash,
						   dl_compare, free)))
	&& hash_insert (delayed_link_table, p)))
    xalloc_die ();

  return 0;
}

static void
replace_symlink_placeholders (void)
{
  struct delayed_link *dl;

  if (!delayed_link_table)
    return;
  for (dl = hash_get_first (delayed_link_table);
       dl;
       dl = hash_get_next (delayed_link_table, dl))
    {
      struct stat st;

      /* Make sure the placeholder file is still there.  If not,
	 don't create a link, as the placeholder was probably
	 removed by a later extraction.  */
      if (lstat (dl->target, &st) == 0
	  && st.st_dev == dl->dev
	  && st.st_ino == dl->ino)
	{
	  if (unlink (dl->target))
	    unlink_error (dl->target);
	  else
	    {
	      int res = UMASKED_SYMLINK (dl->source, dl->target, dl->mode);
	      if (res < 0 && create_dir_flag)
		{
		  create_all_directories (dl->target);
		  res = UMASKED_SYMLINK (dl->source, dl->target, dl->mode);
		}
	      if (res < 0)
		symlink_error (dl->source, dl->target);
	      else
		{
		  if (!no_chown_flag)
		    {
		      uid_t uid = set_owner_flag ? set_owner : dl->uid;
		      gid_t gid = set_group_flag ? set_group : dl->gid;
		      if (lchown (dl->target, uid, gid) < 0 && errno != EPERM)
			chown_error_details (dl->target, uid, gid);
		    }
		  if (retain_time_flag)
		    set_file_times (-1, dl->target, dl->mtime, dl->mtime,
				    AT_SYMLINK_NOFOLLOW);
		}
	    }
	}
    }

  hash_free (delayed_link_table);
  delayed_link_table = NULL;
}

static void
copyin_link (struct cpio_file_stat *file_hdr, int in_file_des)
{
  char *link_name = NULL;	/* Name of hard and symbolic links.  */
  int res;			/* Result of various function calls.  */

  if (archive_format != arf_tar && archive_format != arf_ustar)
    {
      if (to_stdout_option)
	{
	  tape_toss_input (in_file_des, file_hdr->c_filesize);
	  tape_skip_padding (in_file_des, file_hdr->c_filesize);
	  return;
	}
      link_name = get_link_name (file_hdr, in_file_des);
      if (!link_name)
	return;
    }
  else
    {
      if (to_stdout_option)
	return;
      link_name = xstrdup (file_hdr->c_tar_linkname);
    }

  if (no_abs_paths_flag)
    symlink_placeholder (link_name, file_hdr->c_name, file_hdr);
  else
    {
      res = UMASKED_SYMLINK (link_name, file_hdr->c_name,
			     file_hdr->c_mode);
      if (res < 0 && create_dir_flag)
	{
	  create_all_directories (file_hdr->c_name);
	  res = UMASKED_SYMLINK (link_name, file_hdr->c_name, file_hdr->c_mode);
	}
      if (res < 0)
	symlink_error (link_name, file_hdr->c_name);
      else if (!no_chown_flag)
	{
	  uid_t uid = set_owner_flag ? set_owner : file_hdr->c_uid;
	  gid_t gid = set_group_flag ? set_group : file_hdr->c_gid;
	  if (lchown (file_hdr->c_name, uid, gid) < 0 && errno != EPERM)
	    chown_error_details (file_hdr->c_name, uid, gid);
	}

      if (retain_time_flag)
	set_file_times (-1, file_hdr->c_name, file_hdr->c_mtime,
			file_hdr->c_mtime, AT_SYMLINK_NOFOLLOW);
    }
  free (link_name);
}

static void
copyin_file (struct cpio_file_stat *file_hdr, int in_file_des)
{
  bool existing_dir = false;

  if (!to_stdout_option
      && try_existing_file (file_hdr, in_file_des, &existing_dir) < 0)
    return;

  /* Do the real copy or link.  */
  switch (file_hdr->c_mode & CP_IFMT)
    {
    case CP_IFREG:
      copyin_regular_file (file_hdr, in_file_des);
      break;

    case CP_IFDIR:
      cpio_create_dir (file_hdr, existing_dir);
      break;

    case CP_IFCHR:
    case CP_IFBLK:
#ifdef CP_IFSOCK
    case CP_IFSOCK:
#endif
#ifdef CP_IFIFO
    case CP_IFIFO:
#endif
      copyin_device (file_hdr);
      break;

#ifdef CP_IFLNK
    case CP_IFLNK:
      copyin_link (file_hdr, in_file_des);
      break;
#endif

    default:
      error (0, 0, _("%s: unknown file type"), file_hdr->c_name);
      tape_toss_input (in_file_des, file_hdr->c_filesize);
      tape_skip_padding (in_file_des, file_hdr->c_filesize);
    }
}


/* Current time for verbose table.  */
static struct timespec current_time;


/* Print the file described by FILE_HDR in long format.
   If LINK_NAME is nonzero, it is the name of the file that
   this file is a symbolic link to.  */

void
long_format (struct cpio_file_stat *file_hdr, char const *link_name)
{
  char mbuf[11];
  time_t when;
  char *tbuf;
  struct timespec when_timespec;
  /* A Gregorian year has 365.2425 * 24 * 60 * 60 == 31556952 seconds
     on the average.  Write this value as an integer constant to
     avoid floating point hassles. */
  struct timespec six_months_ago = {
    .tv_sec = current_time.tv_sec - 31556952 / 2,
    .tv_nsec = current_time.tv_nsec
  };

  mode_string (file_hdr->c_mode, mbuf);
  mbuf[10] = '\0';

  printf ("%s %3ju ", mbuf, (uintmax_t) file_hdr->c_nlink);

  if (numeric_uid)
    printf ("%-8ju %-8ju ",
	    (uintmax_t) file_hdr->c_uid,
	    (uintmax_t) file_hdr->c_gid);
  else
    printf ("%-8.8s %-8.8s ", getuser (file_hdr->c_uid),
	    getgroup (file_hdr->c_gid));

  if ((file_hdr->c_mode & CP_IFMT) == CP_IFCHR
      || (file_hdr->c_mode & CP_IFMT) == CP_IFBLK)
    printf ("%3ju, %3ju ",
	    (uintmax_t) file_hdr->c_rdev_maj,
	    (uintmax_t) file_hdr->c_rdev_min);
  else
    printf ("%8ju ", (uintmax_t) file_hdr->c_filesize);

  when = file_hdr->c_mtime;
  when_timespec.tv_sec = when;
  when_timespec.tv_nsec = 0;

  /* Get time values ready to print.  Do not worry about ctime failing,
     or a year outside the range 1000-9999, since 0 <= WHEN < 2**33.  */
  tbuf = ctime (&when);

  /* If the file appears to be in the future, update the current
     time, in case the file happens to have been modified since
     the last time we checked the clock.  */
  if (timespec_cmp (current_time, when_timespec) < 0)
    current_time = current_timespec ();

  /* Consider a time to be recent if it is within the past six months.
     Use the same algorithm that GNU 'ls' does, for consistency. */
  if (!(timespec_cmp (six_months_ago, when_timespec) < 0
	&& timespec_cmp (when_timespec, current_time) < 0))
    {
      /* The file is older than 6 months, or in the future.
	 Show the year instead of the time of day.  */
      memcpy (tbuf + 11, tbuf + 19, sizeof " 1970" - 1);
    }
  tbuf[16] = ' ';
  tbuf[17] = '\0';
  printf ("%s", tbuf + 4);

  printf ("%s", quotearg (file_hdr->c_name));
  if (link_name)
    {
      printf (" -> ");
      printf ("%s", quotearg (link_name));
    }
  putchar ('\n');
}

/* Read a pattern file (for the -E option).  Put a list of
   `num_patterns' elements in `save_patterns'.  Any patterns that were
   already in `save_patterns' (from the command line) are preserved.  */

static void
read_pattern_file (void)
{
  char **new_save_patterns = NULL;
  size_t max_new_patterns;
  size_t new_num_patterns;
  int i;
  dynamic_string pattern_name = DYNAMIC_STRING_INITIALIZER;
  FILE *pattern_fp;

  if (num_patterns < 0)
    num_patterns = 0;
  new_num_patterns = num_patterns;
  max_new_patterns = num_patterns;
  new_save_patterns = xcalloc (max_new_patterns, sizeof (new_save_patterns[0]));

  pattern_fp = fopen (pattern_file_name, "r");
  if (pattern_fp == NULL)
    open_fatal (pattern_file_name);
  while (ds_fgetstr (pattern_fp, &pattern_name, '\n') != NULL)
    {
      if (new_num_patterns == max_new_patterns)
	new_save_patterns = x2nrealloc (new_save_patterns,
					&max_new_patterns,
					sizeof (new_save_patterns[0]));
      new_save_patterns[new_num_patterns] = xstrdup (pattern_name.ds_string);
      ++new_num_patterns;
    }

  ds_free (&pattern_name);

  if (ferror (pattern_fp) || fclose (pattern_fp) == EOF)
    close_error (pattern_file_name);

  for (i = 0; i < num_patterns; ++i)
    new_save_patterns[i] = save_patterns[i];

  save_patterns = new_save_patterns;
  num_patterns = new_num_patterns;
}


uintmax_t
from_ascii (char const *where, size_t digs, unsigned logbase)
{
  uintmax_t value = 0;
  char const *buf = where;
  char const *end = buf + digs;
  int overflow = 0;
  static char codetab[] = "0123456789ABCDEF";

  for (; buf < end && *buf == ' '; buf++)
    ;

  if (buf == end || *buf == 0)
    return 0;
  while (1)
    {
      unsigned d;

      char *p = strchr (codetab, toupper (*buf));
      if (!p)
	{
	  error (0, 0, _("Malformed number %.*s"), (int) digs, where);
	  break;
	}

      d = p - codetab;
      if ((d >> logbase) > 1)
	{
	  error (0, 0, _("Malformed number %.*s"), (int) digs, where);
	  break;
	}
      value += d;
      if (++buf == end || *buf == 0)
	break;
      overflow |= value ^ (value << logbase >> logbase);
      value <<= logbase;
    }
  if (overflow)
    error (0, 0, _("Archive value %.*s is out of range"),
	   (int) digs, where);
  return value;
}



/* Return 16-bit integer I with the bytes swapped.  */
#define swab_short(i) ((((i) << 8) & 0xff00) | (((i) >> 8) & 0x00ff))

/* Read the header, including the name of the file, from file
   descriptor IN_DES into FILE_HDR.  */

void
read_in_header (struct cpio_file_stat *file_hdr, int in_des)
{
  union {
    char str[6];
    unsigned short num;
    struct old_cpio_header old_header;
  } magic;
  long bytes_skipped = 0;	/* Bytes of junk found before magic number.  */

  /* Search for a valid magic number.  */

  if (archive_format == arf_unknown)
    {
      union
      {
	char s[512];
	unsigned short us;
      }	tmpbuf;
      int check_tar;
      int peeked_bytes;

      while (archive_format == arf_unknown)
	{
	  peeked_bytes = tape_buffered_peek (tmpbuf.s, in_des, 512);
	  if (peeked_bytes < 6)
	    error (PAXEXIT_FAILURE, 0, _("premature end of archive"));

	  if (!strncmp (tmpbuf.s, "070701", 6))
	    archive_format = arf_newascii;
	  else if (!strncmp (tmpbuf.s, "070707", 6))
	    archive_format = arf_oldascii;
	  else if (!strncmp (tmpbuf.s, "070702", 6))
	    {
	      archive_format = arf_crcascii;
	      crc_i_flag = true;
	    }
	  else if (tmpbuf.us == 070707
		   || tmpbuf.us == swab_short ((unsigned short) 070707))
	    archive_format = arf_binary;
	  else if (peeked_bytes >= 512
		   && (check_tar = is_tar_header (tmpbuf.s)))
	    {
	      if (check_tar == 2)
		archive_format = arf_ustar;
	      else
		archive_format = arf_tar;
	    }
	  else
	    {
	      tape_buffered_read (tmpbuf.s, in_des, 1L);
	      ++bytes_skipped;
	    }
	}
    }

  if (archive_format == arf_tar || archive_format == arf_ustar)
    {
      if (append_flag)
	last_header_start = input_bytes - io_block_size +
	  (in_buff - input_buffer);
      if (bytes_skipped > 0)
	warn_junk_bytes (bytes_skipped);

      read_in_tar_header (file_hdr, in_des);
      return;
    }

  file_hdr->c_tar_linkname = NULL;

  tape_buffered_read (magic.str, in_des, sizeof (magic.str));
  while (1)
    {
      if (append_flag)
	last_header_start = input_bytes - io_block_size
	  + (in_buff - input_buffer) - 6;
      if (archive_format == arf_newascii
	  && !strncmp (magic.str, "070701", 6))
	{
	  if (bytes_skipped > 0)
	    warn_junk_bytes (bytes_skipped);
	  file_hdr->c_magic = 070701;
	  read_in_new_ascii (file_hdr, in_des);
	  break;
	}
      if (archive_format == arf_crcascii
	  && !strncmp (magic.str, "070702", 6))
	{
	  if (bytes_skipped > 0)
	    warn_junk_bytes (bytes_skipped);
	  file_hdr->c_magic = 070702;
	  read_in_new_ascii (file_hdr, in_des);
	  break;
	}
      if ( (archive_format == arf_oldascii || archive_format == arf_hpoldascii)
	  && !strncmp (magic.str, "070707", 6))
	{
	  if (bytes_skipped > 0)
	    warn_junk_bytes (bytes_skipped);
	  file_hdr->c_magic = 070707;
	  read_in_old_ascii (file_hdr, in_des);
	  break;
	}
      if ( (archive_format == arf_binary || archive_format == arf_hpbinary)
	  && (magic.num == 070707
	      || magic.num == swab_short ((unsigned short) 070707)))
	{
	  /* Having to skip 1 byte because of word alignment is normal.  */
	  if (bytes_skipped > 0)
	    warn_junk_bytes (bytes_skipped);
	  file_hdr->c_magic = 070707;
	  read_in_binary (file_hdr, &magic.old_header, in_des);
	  break;
	}
      bytes_skipped++;
      memmove (magic.str, magic.str + 1, sizeof (magic.str) - 1);
      tape_buffered_read (magic.str + sizeof (magic.str) - 1, in_des, 1L);
    }
}

static void
read_name_from_file (struct cpio_file_stat *file_hdr, int fd, uintmax_t len)
{
  if (len == 0)
    {
      error (0, 0, _("malformed header: file name of zero length"));
    }
  else
    {
      cpio_realloc_c_name (file_hdr, len);
      tape_buffered_read (file_hdr->c_name, fd, len);
      if (file_hdr->c_name[len-1] != 0)
	{
	  error (0, 0, _("malformed header: file name is not nul-terminated"));
	  /* Skip this file */
	  len = 0;
	}
    }
  file_hdr->c_namesize = len;
}

/* Fill in FILE_HDR by reading an old-format ASCII format cpio header from
   file descriptor IN_DES, except for the magic number, which is
   already filled in.  */

void
read_in_old_ascii (struct cpio_file_stat *file_hdr, int in_des)
{
  struct old_ascii_header ascii_header;
  unsigned long dev;

  tape_buffered_read (ascii_header.c_dev, in_des,
		      sizeof ascii_header - sizeof ascii_header.c_magic);
  dev = FROM_OCTAL (ascii_header.c_dev);
  file_hdr->c_dev_maj = major (dev);
  file_hdr->c_dev_min = minor (dev);

  file_hdr->c_ino = FROM_OCTAL (ascii_header.c_ino);
  file_hdr->c_mode = FROM_OCTAL (ascii_header.c_mode);
  file_hdr->c_uid = FROM_OCTAL (ascii_header.c_uid);
  file_hdr->c_gid = FROM_OCTAL (ascii_header.c_gid);
  file_hdr->c_nlink = FROM_OCTAL (ascii_header.c_nlink);
  dev = FROM_OCTAL (ascii_header.c_rdev);
  file_hdr->c_rdev_maj = major (dev);
  file_hdr->c_rdev_min = minor (dev);

  file_hdr->c_mtime = FROM_OCTAL (ascii_header.c_mtime);
  file_hdr->c_filesize = FROM_OCTAL (ascii_header.c_filesize);
  read_name_from_file (file_hdr, in_des, FROM_OCTAL (ascii_header.c_namesize));

  /* HP/UX cpio creates archives that look just like ordinary archives,
     but for devices it sets major = 0, minor = 1, and puts the
     actual major/minor number in the filesize field.  See if this
     is an HP/UX cpio archive, and if so fix it.  We have to do this
     here because process_copy_in() assumes filesize is always 0
     for devices.  */
  switch (file_hdr->c_mode & CP_IFMT)
    {
      case CP_IFCHR:
      case CP_IFBLK:
#ifdef CP_IFSOCK
      case CP_IFSOCK:
#endif
#ifdef CP_IFIFO
      case CP_IFIFO:
#endif
	if (file_hdr->c_filesize != 0
	    && file_hdr->c_rdev_maj == 0
	    && file_hdr->c_rdev_min == 1)
	  {
	    file_hdr->c_rdev_maj = major (file_hdr->c_filesize);
	    file_hdr->c_rdev_min = minor (file_hdr->c_filesize);
	    file_hdr->c_filesize = 0;
	  }
	break;
      default:
	break;
    }
}

/* Fill in FILE_HDR by reading a new-format ASCII format cpio header from
   file descriptor IN_DES, except for the magic number, which is
   already filled in.  */

void
read_in_new_ascii (struct cpio_file_stat *file_hdr, int in_des)
{
  struct new_ascii_header ascii_header;

  tape_buffered_read (ascii_header.c_ino, in_des,
		      sizeof ascii_header - sizeof ascii_header.c_magic);

  file_hdr->c_ino = FROM_HEX (ascii_header.c_ino);
  file_hdr->c_mode = FROM_HEX (ascii_header.c_mode);
  file_hdr->c_uid = FROM_HEX (ascii_header.c_uid);
  file_hdr->c_gid = FROM_HEX (ascii_header.c_gid);
  file_hdr->c_nlink = FROM_HEX (ascii_header.c_nlink);
  file_hdr->c_mtime = FROM_HEX (ascii_header.c_mtime);
  file_hdr->c_filesize = FROM_HEX (ascii_header.c_filesize);
  file_hdr->c_dev_maj = FROM_HEX (ascii_header.c_dev_maj);
  file_hdr->c_dev_min = FROM_HEX (ascii_header.c_dev_min);
  file_hdr->c_rdev_maj = FROM_HEX (ascii_header.c_rdev_maj);
  file_hdr->c_rdev_min = FROM_HEX (ascii_header.c_rdev_min);
  file_hdr->c_chksum = FROM_HEX (ascii_header.c_chksum);
  read_name_from_file (file_hdr, in_des, FROM_HEX (ascii_header.c_namesize));

  /* In SVR4 ASCII format, the amount of space allocated for the header
     is rounded up to the next long-word, so we might need to drop
     1-3 bytes.  */
  tape_skip_padding (in_des, file_hdr->c_namesize + 110);
}

/* Fill in FILE_HDR by reading a binary format cpio header from
   file descriptor IN_DES, except for the first 6 bytes (the magic
   number, device, and inode number), which are already filled in.  */

void
read_in_binary (struct cpio_file_stat *file_hdr,
		struct old_cpio_header *short_hdr,
		int in_des)
{
  file_hdr->c_magic = short_hdr->c_magic;

  tape_buffered_read (((char *) short_hdr) + 6, in_des,
		      sizeof *short_hdr - 6 /* = 20 */);

  /* If the magic number is byte swapped, fix the header.  */
  if (file_hdr->c_magic == swab_short ((unsigned short) 070707))
    {
      static int warned = 0;

      /* Alert the user that they might have to do byte swapping on
	 the file contents.  */
      if (warned == 0)
	{
	  error (0, 0, _("warning: archive header has reverse byte-order"));
	  warned = 1;
	}
      swab_array ((char *) short_hdr, 13);
    }

  file_hdr->c_dev_maj = major (short_hdr->c_dev);
  file_hdr->c_dev_min = minor (short_hdr->c_dev);
  file_hdr->c_ino = short_hdr->c_ino;
  file_hdr->c_mode = short_hdr->c_mode;
  file_hdr->c_uid = short_hdr->c_uid;
  file_hdr->c_gid = short_hdr->c_gid;
  file_hdr->c_nlink = short_hdr->c_nlink;
  file_hdr->c_rdev_maj = major (short_hdr->c_rdev);
  file_hdr->c_rdev_min = minor (short_hdr->c_rdev);
  file_hdr->c_mtime = (unsigned long) short_hdr->c_mtimes[0] << 16
		      | short_hdr->c_mtimes[1];
  file_hdr->c_filesize = (unsigned long) short_hdr->c_filesizes[0] << 16
		      | short_hdr->c_filesizes[1];
  read_name_from_file (file_hdr, in_des, short_hdr->c_namesize);

  /* In binary mode, the amount of space allocated in the header for
     the filename is `c_namesize' rounded up to the next short-word,
     so we might need to drop a byte.  */
  if (file_hdr->c_namesize % 2)
    tape_toss_input (in_des, 1L);

  /* HP/UX cpio creates archives that look just like ordinary archives,
     but for devices it sets major = 0, minor = 1, and puts the
     actual major/minor number in the filesize field.  See if this
     is an HP/UX cpio archive, and if so fix it.  We have to do this
     here because process_copy_in() assumes filesize is always 0
     for devices.  */
  switch (file_hdr->c_mode & CP_IFMT)
    {
      case CP_IFCHR:
      case CP_IFBLK:
#ifdef CP_IFSOCK
      case CP_IFSOCK:
#endif
#ifdef CP_IFIFO
      case CP_IFIFO:
#endif
	if (file_hdr->c_filesize != 0
	    && file_hdr->c_rdev_maj == 0
	    && file_hdr->c_rdev_min == 1)
	  {
	    file_hdr->c_rdev_maj = major (file_hdr->c_filesize);
	    file_hdr->c_rdev_min = minor (file_hdr->c_filesize);
	    file_hdr->c_filesize = 0;
	  }
	break;
      default:
	break;
    }
}

/* Exchange the bytes of each element of the array of COUNT shorts
   starting at PTR.  */

void
swab_array (char *ptr, int count)
{
  char tmp;

  while (count-- > 0)
    {
      tmp = *ptr;
      *ptr = *(ptr + 1);
      ++ptr;
      *ptr = tmp;
      ++ptr;
    }
}

/* Read the collection from standard input and create files
   in the file system.  */

void
process_copy_in (void)
{
  FILE *tty_in = NULL;		/* Interactive file for rename option.  */
  FILE *tty_out = NULL;		/* Interactive file for rename option.  */
  FILE *rename_in = NULL;	/* Batch file for rename option.  */
  struct stat file_stat;	/* Output file stat record.  */
  struct cpio_file_stat file_hdr = CPIO_FILE_STAT_INITIALIZER;
				/* Output header information.  */
  int in_file_des;		/* Input file descriptor.  */
  char skip_file;		/* Flag for use with patterns.  */
  int i;			/* Loop index variable.  */

  newdir_umask = umask (0);     /* Reset umask to preserve modes of
				   created files  */

  /* Initialize the copy in.  */
  if (pattern_file_name)
    {
      read_pattern_file ();
    }

  if (rename_batch_file)
    {
      rename_in = fopen (rename_batch_file, "r");
      if (rename_in == NULL)
	{
	  error (PAXEXIT_FAILURE, errno, TTY_NAME);
	}
    }
  else if (rename_flag)
    {
      /* Open interactive file pair for rename operation.  */
      tty_in = fopen (TTY_NAME, "r");
      if (tty_in == NULL)
	{
	  error (PAXEXIT_FAILURE, errno, TTY_NAME);
	}
      tty_out = fopen (TTY_NAME, "w");
      if (tty_out == NULL)
	{
	  error (PAXEXIT_FAILURE, errno, TTY_NAME);
	}
    }

  /* Get date and time if needed for processing the table option.  */
  if (table_flag && verbose_flag)
    current_time = current_timespec ();

  /* Check whether the input file might be a tape.  */
  in_file_des = archive_des;
  if (_isrmt (in_file_des))
    {
      input_is_special = 1;
      input_is_seekable = 0;
    }
  else
    {
      if (fstat (in_file_des, &file_stat))
	error (PAXEXIT_FAILURE, errno, _("standard input is closed"));
      input_is_special =
#ifdef S_ISBLK
	S_ISBLK (file_stat.st_mode) ||
#endif
	S_ISCHR (file_stat.st_mode);
      input_is_seekable = S_ISREG (file_stat.st_mode);
    }
  output_is_seekable = true;

  change_dir ();

  /* While there is more input in the collection, process the input.  */
  while (1)
    {
      swapping_halfwords = swapping_bytes = false;

      /* Start processing the next file by reading the header.  */
      read_in_header (&file_hdr, in_file_des);

#ifdef DEBUG_CPIO
      if (debug_flag)
	{
	  struct cpio_file_stat *h;
	  h = &file_hdr;
	  fprintf (stderr,
		"magic = 0%o, ino = %ld, mode = 0%o, uid = %d, gid = %d\n",
		h->c_magic, (long)h->c_ino, h->c_mode, h->c_uid, h->c_gid);
	  fprintf (stderr,
		"nlink = %d, mtime = %d, filesize = %d, dev_maj = 0x%x\n",
		h->c_nlink, h->c_mtime, h->c_filesize, h->c_dev_maj);
	  fprintf (stderr,
		"dev_min = 0x%x, rdev_maj = 0x%x, rdev_min = 0x%x, namesize = %d\n",
		h->c_dev_min, h->c_rdev_maj, h->c_rdev_min, h->c_namesize);
	  fprintf (stderr,
		"chksum = %d, name = \"%s\", tar_linkname = \"%s\"\n",
		h->c_chksum, h->c_name,
		h->c_tar_linkname ? h->c_tar_linkname : "(null)" );

	}
#endif
      if (file_hdr.c_namesize == 0)
	skip_file = true;
      else
	{
	  /* Is this the header for the TRAILER file?  */
	  if (strcmp (CPIO_TRAILER_NAME, file_hdr.c_name) == 0)
	    break;

	  cpio_safer_name_suffix (file_hdr.c_name, false, !no_abs_paths_flag,
				  false);

	  /* Does the file name match one of the given patterns?  */
	  if (num_patterns <= 0)
	    skip_file = false;
	  else
	    {
	      skip_file = copy_matching_files;
	      for (i = 0; i < num_patterns
		     && skip_file == copy_matching_files; i++)
		{
		  if (fnmatch (save_patterns[i], file_hdr.c_name, 0) == 0)
		    skip_file = !copy_matching_files;
		}
	    }
	}

      if (skip_file)
	{
	  /* If we're skipping a file with links, there might be other
	     links that we didn't skip, and this file might have the
	     data for the links.  If it does, we'll copy in the data
	     to the links, but not to this file.  */
	  if (file_hdr.c_nlink > 1 && (archive_format == arf_newascii
	      || archive_format == arf_crcascii) )
	    {
	      if (create_defered_links_to_skipped(&file_hdr, in_file_des) < 0)
		{
		  tape_toss_input (in_file_des, file_hdr.c_filesize);
		  tape_skip_padding (in_file_des, file_hdr.c_filesize);
		}
	    }
	  else
	    {
	      tape_toss_input (in_file_des, file_hdr.c_filesize);
	      tape_skip_padding (in_file_des, file_hdr.c_filesize);
	    }
	}
      else if (table_flag)
	{
	  list_file (&file_hdr, in_file_des);
	}
      else if (append_flag)
	{
	  tape_toss_input (in_file_des, file_hdr.c_filesize);
	  tape_skip_padding (in_file_des, file_hdr.c_filesize);
	}
      else if (only_verify_crc_flag)
	{
#ifdef CP_IFLNK
	  if ((file_hdr.c_mode & CP_IFMT) == CP_IFLNK)
	    {
	      if (archive_format != arf_tar && archive_format != arf_ustar)
		{
		  tape_toss_input (in_file_des, file_hdr.c_filesize);
		  tape_skip_padding (in_file_des, file_hdr.c_filesize);
		  continue;
		}
	    }
#endif
	    crc = 0;
	    tape_toss_input (in_file_des, file_hdr.c_filesize);
	    tape_skip_padding (in_file_des, file_hdr.c_filesize);
	    if (crc != file_hdr.c_chksum)
	      {
		error (0, 0, _("%s: checksum error (0x%x, should be 0x%x)"),
		       file_hdr.c_name, crc, file_hdr.c_chksum);
	      }
	 /* Debian hack: -v and -V now work with --only-verify-crc.
	    (99/11/10) -BEM */
	    if (verbose_flag)
	      {
		fprintf (stderr, "%s\n", file_hdr.c_name);
	      }
	    if (dot_flag)
	      {
		fputc ('.', stderr);
	      }
	}
      else
	{
	  /* Copy the input file into the directory structure.  */

	  /* Do we need to rename the file? */
	  if (rename_flag || rename_batch_file)
	    {
	      if (query_rename(&file_hdr, tty_in, tty_out, rename_in) < 0)
		{
		  tape_toss_input (in_file_des, file_hdr.c_filesize);
		  tape_skip_padding (in_file_des, file_hdr.c_filesize);
		  continue;
		}
	    }

	  copyin_file(&file_hdr, in_file_des);

	  if (verbose_flag)
	    fprintf (stderr, "%s\n", file_hdr.c_name);
	  if (dot_flag)
	    fputc ('.', stderr);
	}
    }

  if (dot_flag)
    fputc ('\n', stderr);

  replace_symlink_placeholders ();
  apply_delayed_set_stat ();

  cpio_file_stat_free (&file_hdr);

  if (append_flag)
    return;

  if (archive_format == arf_newascii || archive_format == arf_crcascii)
    {
      create_final_defers ();
    }
  if (!quiet_flag)
    {
      size_t blocks;
      blocks = (input_bytes + io_block_size - 1) / io_block_size;
      fprintf (stderr,
	       ngettext ("%lu block\n", "%lu blocks\n",
			 (unsigned long) blocks),
	       (unsigned long) blocks);
    }
}
