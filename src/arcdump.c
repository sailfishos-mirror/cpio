#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>

int
main (int argc, char **argv)
{
  int c;
  int n = 0;
  static char xdig[] = "0123456789abcdef";
  while ((c = getchar ()) != EOF)
    {
      unsigned char b = (unsigned) c;
      putchar ('0');
      putchar ('x');
      putchar (xdig[b >> 4]);
      putchar (xdig[b & 0xf]);
      putchar (',');
      if ((n = (n + 1) % 8) == 0)
	putchar ('\n');
    }
  return 0;
}
