# Makefile for GNU tar library.			-*- Makefile -*-

# Copyright (C) 1994-2025 Free Software Foundation, Inc.

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.

# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License along
# with this program.  If not, see <http://www.gnu.org/licenses/>.

noinst_LIBRARIES=libpax.a
rmt-command.h : Makefile
	$(AM_V_GEN)rm -f $@-t $@
	$(AM_V_at)echo "#ifndef DEFAULT_RMT_COMMAND" >> $@-t
	$(AM_V_at)echo "# define DEFAULT_RMT_COMMAND \"$(DEFAULT_RMT_DIR)/`echo rmt | sed '$(transform)'`$(EXEEXT)\"" >> $@-t
	$(AM_V_at)echo "#endif" >> $@-t
	$(AM_V_at)mv $@-t $@
BUILT_SOURCES = rmt-command.h
CLEANFILES = rmt-command.h rmt-command.h-t
AM_CPPFLAGS = -I$(top_srcdir)/gnu -I../ -I../gnu 

noinst_HEADERS = system.h system-ioctl.h rmt.h paxlib.h
libpax_a_SOURCES = \
 paxexit.c\
 paxexit-status.c\
 paxlib.h\
 rtapelib.c\
 sysdep.h\
 sysdep.c\
 system.h\
 paxerror.c\
 paxnames.c
