/* This program is part of cpio testsuite.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <assert.h>

int
main (int argc, char **argv)
{
  size_t s = SIZE_MAX;
  int c;

  while ((c = getopt (argc, argv, "hbi")) != EOF)
    {
      switch (c)
	{
	case 'h':
	  s /= 2;
	  break;

	case 'b':
	  s /= 512;
	  break;

	case 'i':
	  assert (s + 1 > s);
	  s++;
	  break;

	default:
	  return 1;
	}
    }

  assert (argc == optind);
  printf ("%zu\n", s);
  return 0;
}
