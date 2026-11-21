dnl Process this file with autoconf to produce a configure script.
AC_INIT
AC_CONFIG_FILES([Makefile.conf])
PKG_CHECK_MODULES([SNDFILE], [sndfile],,
    [AC_MSG_ERROR([libsndfile development files not found])])
AC_OUTPUT
