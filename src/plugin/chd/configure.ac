dnl Process this file with autoconf to produce a configure script.
AC_INIT
AC_CONFIG_FILES([Makefile.conf])
PKG_CHECK_MODULES([CHD], [libchdr],, [
    AC_CHECK_HEADER([libchdr/chd.h],,
        [AC_MSG_ERROR([libchdr development files not found])])
    AC_CHECK_LIB([chdr], [chd_open], [CHD_LIBS=-lchdr],
        [AC_MSG_ERROR([libchdr not found])])
    AC_SUBST([CHD_CFLAGS])
    AC_SUBST([CHD_LIBS])
])
AC_OUTPUT
