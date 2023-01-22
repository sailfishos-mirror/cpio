/* dstring.h - Dynamic string handling include file.  Requires strings.h.
   Copyright (C) 1990-2023 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with this program; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301 USA.  */

/* A dynamic string consists of record that records the size of an
   allocated string and the pointer to that string.  The actual string
   is a normal zero byte terminated string that can be used with the
   usual string functions.  The major difference is that the
   dynamic_string routines know how to get more space if it is needed
   by allocating new space and copying the current string.  */

typedef struct
{
  size_t ds_size;   /* Actual amount of storage allocated.  */
  size_t ds_idx;    /* Index of the next free byte in the string. */
  char *ds_string;  /* String storage. */
} dynamic_string;

#define DYNAMIC_STRING_INITIALIZER { 0, 0, NULL }

void ds_init (dynamic_string *string);
void ds_free (dynamic_string *string);
void ds_reset (dynamic_string *s, size_t len);

/* All functions below guarantee that s->ds_string[s->ds_idx] == '\0' */
char *ds_fgetname (FILE *f, dynamic_string *s);
char *ds_fgets (FILE *f, dynamic_string *s);
char *ds_fgetstr (FILE *f, dynamic_string *s, char eos);
void ds_append (dynamic_string *s, int c);
void ds_concat (dynamic_string *s, char const *str);

#define ds_len(s) ((s)->ds_idx)

int ds_endswith (dynamic_string *s, int c);

