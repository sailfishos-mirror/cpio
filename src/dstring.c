/* dstring.c - The dynamic string handling routines used by cpio.
   Copyright (C) 1990-2024 Free Software Foundation, Inc.

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

#if defined(HAVE_CONFIG_H)
# include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#if defined(HAVE_STRING_H) || defined(STDC_HEADERS)
#include <string.h>
#else
#include <strings.h>
#endif
#include "dstring.h"
#include <xalloc.h>

/* Initialiaze dynamic string STRING with space for SIZE characters.  */

void
ds_init (dynamic_string *string)
{
  memset (string, 0, sizeof *string);
}

/* Free the dynamic string storage. */

void
ds_free (dynamic_string *string)
{
  free (string->ds_string);
}

/* Expand dynamic string STRING, if necessary.  */

void
ds_resize (dynamic_string *string, size_t len)
{
  while (len + string->ds_idx >= string->ds_size)
    {
      string->ds_string = x2nrealloc (string->ds_string, &string->ds_size,
				      1);
    }
}

/* Reset the index of the dynamic string S to LEN. */

void
ds_reset (dynamic_string *s, size_t len)
{
  ds_resize (s, len);
  s->ds_idx = len;
}

/* Dynamic string S gets a string terminated by the EOS character
   (which is removed) from file F.  S will increase
   in size during the function if the string from F is longer than
   the current size of S.
   Return NULL if end of file is detected.  Otherwise,
   Return a pointer to the null-terminated string in S.  */

char *
ds_fgetstr (FILE *f, dynamic_string *s, char eos)
{
  int next_ch;

  /* Initialize.  */
  s->ds_idx = 0;

  /* Read the input string.  */
  while ((next_ch = getc (f)) != eos && next_ch != EOF)
    {
      ds_resize (s, 0);
      s->ds_string[s->ds_idx++] = next_ch;
    }
  ds_resize (s, 0);
  s->ds_string[s->ds_idx] = '\0';

  if (s->ds_idx == 0 && next_ch == EOF)
    return NULL;
  else
    return s->ds_string;
}

void
ds_append (dynamic_string *s, int c)
{
  ds_resize (s, 0);
  s->ds_string[s->ds_idx] = c;
  if (c)
    {
      s->ds_idx++;
      ds_resize (s, 0);
      s->ds_string[s->ds_idx] = 0;
    }
}

void
ds_concat (dynamic_string *s, char const *str)
{
  size_t len = strlen (str);
  ds_resize (s, len);
  memcpy (s->ds_string + s->ds_idx, str, len);
  s->ds_idx += len;
  s->ds_string[s->ds_idx] = 0;
}

char *
ds_fgets (FILE *f, dynamic_string *s)
{
  return ds_fgetstr (f, s, '\n');
}

char *
ds_fgetname (FILE *f, dynamic_string *s)
{
  return ds_fgetstr (f, s, '\0');
}

/* Return true if the dynamic string S ends with character C. */
int
ds_endswith (dynamic_string *s, int c)
{
  return (s->ds_idx > 0 && s->ds_string[s->ds_idx - 1] == c);
}
