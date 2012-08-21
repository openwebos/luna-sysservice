#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

# @@@LICENSE
#
# Copyright (c) 2010 - 2012 Hewlett-Packard Development Company, L.P.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

AC_PREREQ(2.61)
AC_INIT(LunaSysService, 0.0)
AC_CONFIG_SRCDIR([Src/Main.cpp])
AC_CONFIG_HEADER([config.h])

# Check for automake and pkgconfig
AM_INIT_AUTOMAKE([foreign])
PKG_PROG_PKG_CONFIG
AC_PROG_LIBTOOL

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.
PKG_CHECK_MODULES(GLIB, glib-2.0)
PKG_CHECK_MODULES(CJSON, json)
PKG_CHECK_MODULES(GDBUS, gdbus)
PKG_CHECK_MODULES(LUNASERVICE, lunaservice)
PKG_CHECK_MODULES(SQLITE, sqlite3)
PKG_CHECK_MODULES(QTCORE, QtCore)
PKG_CHECK_MODULES(QTGUI, QtGui)

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(CJSON_CFLAGS)
AC_SUBST(CJSON_LIBS)
AC_SUBST(GDBUS_CFLAGS)
AC_SUBST(GDBUS_LIBS)
AC_SUBST(LUNASERVICE_CFLAGS)
AC_SUBST(LUNASERVICE_LIBS)
AC_SUBST(SQLITE_CFLAGS)
AC_SUBST(SQLITE_LIBS)

AC_SUBST(QTCORE_CFLAGS)
AC_SUBST(QTCORE_LIBS)
AC_SUBST(QTGUI_CFLAGS)
AC_SUBST(QTGUI_LIBS)
#AC_CHECK_LIB(sqlite3, sqlite3_open)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_HEADER_TIME
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_LSTAT
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK
AC_FUNC_MKTIME
AC_CHECK_FUNCS([localtime_r setenv strdup strtol tzset])

AC_CONFIG_FILES([Makefile Src/Makefile])
AC_OUTPUT
