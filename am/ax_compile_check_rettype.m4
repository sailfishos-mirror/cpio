# SYNOPSIS
#
#   AC_COMPILE_RETTYPE(FUNC, ARGS [, HEADERS [, EXTRA_TYPES...]])
#
# DESCRIPTION
#
#   This macro finds the integer type that can be used to represent the
#   value returned by FUNC (a C function or define).  It uses only
#   compile checks (AC_COMPILE_IFELSE) and is safe to use when cross-
#   compiling.  The ARGS parameter gives arguments to supply to the
#   function call.  Obviously, these must be immediate values.
#   HEADERS supplies additional headers to include.
#
#   If a matching integer type was found, the macro defines
#   RETTYPE_`TYPE' to its name. Otherwise, an error is reported.
#
# EXAMPLE
#
#   AC_COMPILE_RETTYPE(major, 0)
#
# REFERENCES
#
#   The macro is derived from AX_COMPILE_CHECK_SIZEOF:
#   https://www.gnu.org/software/autoconf-archive/ax_compile_check_sizeof.html
#
# LICENSE
#
#   Copyright (c) 2008 Kaveh Ghazi <ghazi@caip.rutgers.edu>
#   Copyright (c) 2017 Reini Urban <rurban@cpan.org>
#   Copyright (c) 2019 Sergey Poznyakoff <gray@gnu.org>
#
#   This program is free software: you can redistribute it and/or modify it
#   under the terms of the GNU General Public License as published by the
#   Free Software Foundation, either version 3 of the License, or (at your
#   option) any later version.
#
#   This program is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
#   Public License for more details.
#
#   You should have received a copy of the GNU General Public License along
#   with this program. If not, see <https://www.gnu.org/licenses/>.
#
#   As a special exception, the respective Autoconf Macro's copyright owner
#   gives unlimited permission to copy, distribute and modify the configure
#   scripts that are the output of Autoconf when processing the Macro. You
#   need not follow the terms of the GNU General Public License when using
#   or distributing such scripts, even though portions of the text of the
#   Macro appear in them. The GNU General Public License (GPL) does govern
#   all other use of the material that constitutes the Autoconf Macro.
#
#   This special exception to the GPL applies to versions of the Autoconf
#   Macro released by the Autoconf Archive. When you make and distribute a
#   modified version of the Autoconf Macro, you may extend this special
#   exception to the GPL to apply to your modified version as well.

#serial 1

AU_ALIAS([AC_COMPILE_CHECK_RETTYPE], [AX_COMPILE_CHECK_RETTYPE])
AC_DEFUN([AX_COMPILE_CHECK_RETTYPE],
[changequote(<<, >>)dnl
dnl The name to #define.
define(<<AC_TYPE_NAME>>, translit(RETTYPE_$1, [a-z *], [A-Z_P]))dnl
dnl The cache variable name.
define(<<AC_CV_NAME>>, translit(ac_cv_rettype_$1, [ *], [_p]))dnl
changequote([, ])dnl
AC_MSG_CHECKING(return type of $1())
AC_CACHE_VAL(AC_CV_NAME,
[for ac_type in char short int long "long long" $4
 do 
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <sys/types.h>
$3
]], [[switch (0) case 0: case (sizeof ($1($2)) == sizeof ($ac_type)):;]])], [AC_CV_NAME=$ac_type])
  if test x$AC_CV_NAME != x ; then break; fi
done
])
if test x$AC_CV_NAME = x ; then
  AC_MSG_ERROR([cannot determine type])
fi
AC_MSG_RESULT($AC_CV_NAME)
AC_DEFINE_UNQUOTED(AC_TYPE_NAME, $AC_CV_NAME, [Data type returned by $1()])
undefine([AC_TYPE_NAME])dnl
undefine([AC_CV_NAME])dnl
])
