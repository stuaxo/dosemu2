dnl Process this file with autoconf to produce a configure script.
AC_INIT([DOSEMU2], m4_esyscmd([tr -d '\n' < VERSION]),
    [https://github.com/stsp/dosemu2/issues], dosemu2)
AC_CONFIG_SRCDIR(src/emu.c)
AC_PREREQ(2.59)

# first check if we are called from default-configure and if not,
# execute that to parse compiletime settings
if test -z "$DOSEMU_DEFAULT_CONFIGURE" ; then
  eval "set -- $ac_configure_args"
  exec $srcdir/default-configure "$@"
fi

AC_CONFIG_HEADER(src/include/config.h)

AC_CANONICAL_BUILD

dnl Checks for programs.
: ${CFLAGS=""}
AC_PROG_CC
dnl ifdef([AC_PROG_CC_C99], [AC_PROG_CC_C99], [CPPFLAGS="$CPPFLAGS -std=gnu99"; export CPPFLAGS])
AC_PROG_CPP

AC_ARG_WITH(target-bits, [  --with-target-bits=bits compile for 32 or 64 bits (default=auto)],
  CPPFLAGS="$CPPFLAGS -m$with_target_bits"
  export CPPFLAGS
  LDFLAGS="$LDFLAGS -m$with_target_bits"
  export LDFLAGS
)
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LEX
if test -z "`echo $LEX | grep flex`" ; then
  AC_CHECK_PROG(lex_ok, $LEX, "yes", "no")
fi
if test "$lex_ok" = "no"; then
  AC_MSG_WARN( )
  AC_MSG_WARN(Your system doesn't seem to have lex or flex available.)
  AC_MSG_ERROR(Install lex or flex and retry.)
fi
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB
AC_PROG_YACC
dnl The bison problem
if test -z "`echo $YACC | grep bison`" ; then
  AC_MSG_WARN( )
  AC_MSG_WARN(Your system doesn't seem to have bison available.)
  AC_MSG_ERROR(Install bison and retry.)
fi

dnl Checks for libraries.

dnl Checks for header files.
AC_SYS_LARGEFILE
if test "$build_cpu" = "x86_64"; then
  AC_CHECK_MEMBER([struct sigcontext.ss],[AC_DEFINE(HAVE_SIGCONTEXT_SS)],,
    [[#include <signal.h>]])
fi

dnl Checks for library functions.
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <time.h>
#include <unistd.h>]], [[
#if !(defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK >= 0 && defined(CLOCK_MONOTONIC))
        #error No monotonic clock
#endif
	return 0;
]])],,[
AC_MSG_ERROR([Sorry, you need glibc with monotonic clock support (2.3.3 or newer).])])

AC_CHECK_LIB(rt, shm_open)
AC_CHECK_FUNCS(shm_open)

dnl Here is where we do our stuff

AC_ARG_WITH(docdir,
  [  --with-docdir=dir       give the directory used to install documentation]
  [                        (default: ${prefix}/share/doc/dosemu)],
  docdir="$withval", docdir="${prefix}/share/doc/dosemu")
AC_ARG_WITH(x11fontdir,
  [  --with-x11fontdir=dir   give the directory used to install the VGA X11 font]
  [                        (default: ${datadir}/dosemu/Xfonts)],
  x11fontdir="$withval", x11fontdir="${datadir}/dosemu/Xfonts")
AC_ARG_WITH(syshdimagedir,
  [  --with-syshdimagedir=dir give the directory used for system wide boot directories and hdimages]
  [                        (default: /var/lib/dosemu)],
  syshdimagedir="$withval", syshdimagedir="/var/lib/dosemu")
AC_ARG_WITH(fdtarball,
  [  --with-fdtarball=file   give the name of the FreeDOS tarball or 'none']
  [                        (default: dosemu-freedos-bin.tgz)],
  fdtarball="$withval", fdtarball="dosemu-freedos-bin.tgz")

AC_SUBST(docdir)
AC_SUBST(x11fontdir)
AC_SUBST(syshdimagedir)
AC_SUBST(fdtarball)

if test "$build_os" = "linux-gnu"; then
  CONFIG_HOST='linux'
fi
AC_SUBST(CONFIG_HOST)

dnl Check for static and then also use it for tests

DOSEMU_CFLAGS="-Wall -Wstrict-prototypes -Wmissing-declarations\
 -Wnested-externs -fms-extensions -fno-stack-protector"
DOSEMU_LDFLAGS="-Wl,-warn-common"
AC_CHECK_LIB(m, pow)

AC_ARG_ENABLE(dlplugins,
              AS_HELP_STRING([--disable-dlplugins], [do NOT use dynamically loaded plugins]))
if test "$enable_dlplugins" != "no"; then
              AC_MSG_NOTICE(Using dynamically loaded plugins...)
              AC_DEFINE(USE_DL_PLUGINS)
              USE_DL_PLUGINS="USE_DL_PLUGINS=1"
              AC_SUBST(USE_DL_PLUGINS)
              DOSBIN_LDFLAGS="-rdynamic"
              AC_SUBST(DOSBIN_LDFLAGS)
              AC_CHECK_LIB(dl, dlopen)
else
              AC_MSG_NOTICE(Not using dynamically loaded plugins...)
fi

dnl check for stack protector and abort
AC_MSG_CHECKING([for stack protector disabled])
AS_IF([[(
    ${CC} ${CFLAGS} ${DOSEMU_CFLAGS} -S -x c -o - - << _EOF_
    int foo(int);
    int foo(int i)
    {
      char a[2];
      a[i]=0;
      return a[i+1];
    }
_EOF_
    ) | grep "%fs:" >/dev/null]],
    [
	AC_MSG_RESULT([no])
	AC_MSG_ERROR([Cannot compile dosemu with stack protector enabled])
    ], AC_MSG_RESULT([yes]))

dnl Check whether we have pthreads and whether to use it
AC_CHECK_LIB(pthread, pthread_create,,[
  AC_MSG_ERROR([No libpthread found, please install glibc-devel package])
])

dnl Check for the XKB extension to get reliable keyboard handling
AC_CHECK_HEADER(X11/XKBlib.h, AC_DEFINE(HAVE_XKB,1))

CONFIG_TIME=`date +"%F %T %z"`
AC_DEFINE_UNQUOTED(CONFIG_HOST, "$CONFIG_HOST")
AC_DEFINE_UNQUOTED(CONFIG_TIME, "$CONFIG_TIME")

REQUIRED="commands tools/periph"

dnl enable EXPERIMENTAL stuff
 AC_ARG_ENABLE(experimental,
               AS_HELP_STRING([--enable-experimental], [enable configuration of EXPERIMENTAL stuff]))
  if test "$enable_experimental" = "yes"; then
   AC_MSG_NOTICE(Allowing EXPERIMENTAL stuff to be configured...);
   AC_DEFINE([EXPERIMENTAL], 1, [ Define this to enable experimental stuff ])
  else
   AC_MSG_NOTICE(EXPERIMENTAL stuff disabled...);
  fi

dnl try to hook in available plug-ins
if test -x $srcdir/mkpluginhooks; then
  $srcdir/mkpluginhooks
  if test -f plugin_libdirs; then
    PLUGINSUBDIRS=`cat plugin_libdirs`
    PLUGINS=`echo $PLUGINSUBDIRS | sed 's/plugin\///g'`
    AC_MSG_NOTICE(Including plugins: $PLUGINS);
    cd src
    for i in $PLUGINSUBDIRS; do
      if ! grep USE_DL_PLUGINS $i/Makefile >/dev/null ; then
        ST_PLUGINSUBDIRS="$ST_PLUGINSUBDIRS $i"
      fi
    done
    cd ..
    if test -f plugin_incdirs; then
      cd src
      for i in `cat ../plugin_incdirs`; do
        if test -f ../configure.ac; then
          for j in $i/*.h; do
            ln -sf ../../$j plugin/include/`basename $j`
          done
        else
	  abssrcdir=`cd .. && cd $srcdir && pwd`
          for j in $abssrcdir/src/$i/*.h; do
            ln -sf $j plugin/include/`basename $j`
          done
        fi
      done
      cd ..
    fi
  else
    AC_MSG_NOTICE(Compiling without plug-ins...)
  fi
else
  AC_MSG_NOTICE(Compiling without plug-ins...)
fi

dnl Do compilation for GDB
AC_ARG_ENABLE(debug,
               AS_HELP_STRING([--enable-debug], [compile with debug info]))
  if test "$enable_debug" = "yes"; then
   AC_MSG_NOTICE(Compiling with debug info...);
  else
   AC_MSG_NOTICE(Compiling without debug info...);
  fi

AC_ARG_ENABLE(asan, [AS_HELP_STRING(--enable-asan, [enable address sanitizer])])

AC_SUBST(OPTIONALSUBDIRS)
AC_SUBST(REQUIRED)
AC_SUBST(PLUGINSUBDIRS)
AC_SUBST(ST_PLUGINSUBDIRS)

dnl Now CFLAGS and INCLUDES

INCDIR="-I\${top_builddir}/src/include -I\${top_builddir}/src/plugin/include"
AC_SUBST(INCDIR)

AC_ARG_WITH(target_cpu, [  --with-target-cpu=CPU	  use the specified target CPU (default=auto)])
if test "$with_target_cpu" = ""; then
  AC_MSG_NOTICE(Compiling with default target CPU...)
  target_cpu=""
else
  AC_MSG_NOTICE(Compiling with specified target CPU...)
  target_cpu=$with_target_cpu
fi

AC_MSG_CHECKING(for $CC actually being clang...)
if "$CC" -v 2>&1|grep 'clang' >/dev/null; then
  if "$CC" -no-integrated-as -E - < /dev/null 2>&1|grep 'clang' >/dev/null; then
    AC_MSG_RESULT([yes])
  else
    ASFLAGS="$ASFLAGS -no-integrated-as"
    AC_MSG_RESULT([yes, using -no-integrated-as for assembly language files.])
  fi
  # unfortunately clang is too buggy even on -O1
  DOSEMU_CFLAGS="$DOSEMU_CFLAGS -Wno-microsoft -O0"
  use_clang="yes"
else
  CCAS="$CC"
  DOSEMU_CFLAGS="$DOSEMU_CFLAGS -fplan9-extensions -Wno-maybe-uninitialized"
  AC_MSG_RESULT([no])
  use_clang="no"
fi

if test "$enable_debug" = "yes" ; then
  DOSEMU_CFLAGS="$DOSEMU_CFLAGS -ggdb3"
  if test -z "$CFLAGS" -a "$use_clang" = "no" ; then
    # simx86 is too slow on gcc without -O
    DOSEMU_CFLAGS="$DOSEMU_CFLAGS -O"
  fi
elif test -z "$CFLAGS" -a "$use_clang" = "no" ; then
  DOSEMU_CFLAGS="$DOSEMU_CFLAGS -O2"
fi

if test "$enable_debug" = "yes"; then
  if test "$enable_asan" = "yes"; then
    DOSEMU_CFLAGS="$DOSEMU_CFLAGS -fsanitize=address"
    LDFLAGS="$LDFLAGS -fsanitize=address"
  fi
  AC_DEFINE(_DEBUG)
fi

AC_DEFINE(_GNU_SOURCE)

DOSEMU_CFLAGS="${DOSEMU_CFLAGS} ${OPT} ${PIPE}"
DOSEMU_CPPFLAGS="${DOSEMU_CPPFLAGS} -MMD -DCFLAGS_STR=\"$DOSEMU_CFLAGS $CFLAGS\""
AC_SUBST(ASFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(DOSEMU_CFLAGS)
AC_SUBST(DOSEMU_CPPFLAGS)
AC_SUBST(DOSEMU_LDFLAGS)
AC_SUBST(LIBS)

dnl Create output files. If you add new ones, please do it in order.

AC_CONFIG_FILES([Makefile.conf])

AC_CONFIG_COMMANDS([Makefile],
[ if ! test -f configure.ac; then
    abssrcdir=`cd $srcdir && pwd`
    mkdir -p `(cd $abssrcdir; find . -name .git -prune -o -name .svn -prune -o -name CVS -prune -o -type d -print)`
    for i in `(cd $abssrcdir; find . -name Makefile)`; do
      echo  ln -sf $abssrcdir/$i $i
      ln -sf $abssrcdir/$i $i
    done
  fi
])

dnl autoheader stuff
AH_TOP([
/*
 * BIG FAT WARNING:
 *
 * These #defines come from configure.ac.
 * If you edit config.h.in, your efforts will become useless the next
 * time you run autoheader or autoreconf.
 *
 * All lines within AH_TOP and AH_BOTTOM are copied unchanged into
 * config.h.in. Read the GNU autoconf documentation to learn more
 * xabout configure & friends.
 *
 */

#ifndef CONFIG_H
#define CONFIG_H 1

#define VERSION_OF(a,b,c,d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define DOSEMU_VERSION_CODE VERSION_OF(VERSION,SUBLEVEL,0,0)
#define IS_DEVEL_RELEASE (DOSEMU_VERSION_CODE && 65536)
#define GCC_VERSION_CODE (__GNUC__ * 1000 + __GNUC_MINOR__)

#ifdef __ASM__
/* trick to comment out prototypes */
#define extern #
#else
#include "extern.h"

#include "confpath.h"
/* not overridable file constants */
#define  DOSEMU_RC          ".dosemurc"       /* per user config file */
#define  OLD_DOS_RC         ".dosrc"          /* old, obsolete user config file */
#define  LOCALDIR_BASE_NAME ".dosemu"         /* base directory in $HOME */
#define  DOSEMU_CONF        "dosemu.conf"     /* standard configuration file */
#define  DOSEMU_USERS       "dosemu.users"    /* access right configuration file */
#define  DEFAULT_CONFIG_SCRIPT "builtin"      /* main configuration script */
#define  DOSEMU_LOGLEVEL    "dosemu.loglevel" /* logging configuration file */
#define  DOSEMU_MIDI        "dosemu-midi"     /* fifo for midi daemon */
#define  DOSEMU_MIDI_IN     "dosemu-midi_in"  /* fifo for midi input */

EXTERN char *config_script_name INIT(DEFAULT_CONFIG_SCRIPT);
EXTERN char *config_script_path INIT(0);
EXTERN char *dosemu_users_file_path INIT("/etc/" DOSEMU_USERS);
EXTERN char *dosemu_loglevel_file_path INIT("/etc/" DOSEMU_LOGLEVEL);
EXTERN char *dosemu_rundir_path INIT("~/" LOCALDIR_BASE_NAME "/run");
EXTERN char *dosemu_localdir_path INIT("~/" LOCALDIR_BASE_NAME);

EXTERN char dosemulib_default[] INIT(DOSEMULIB_DEFAULT);
EXTERN char *dosemu_lib_dir_path INIT(dosemulib_default);
EXTERN char dosemuhdimage_default[] INIT(DOSEMUHDIMAGE_DEFAULT);
EXTERN char *dosemu_hdimage_dir_path INIT(dosemuhdimage_default);
EXTERN char keymaploadbase_default[] INIT(DOSEMULIB_DEFAULT "/");
EXTERN char *keymap_load_base_path INIT(keymaploadbase_default);
EXTERN char *keymap_dir_path INIT("keymap/");
EXTERN char *owner_tty_locks INIT("uucp");
EXTERN char *tty_locks_dir_path INIT("/var/lock");
EXTERN char *tty_locks_name_path INIT("LCK..");
EXTERN char *dexe_load_path INIT(dosemuhdimage_default);
EXTERN char *dosemu_midi_path INIT("~/" LOCALDIR_BASE_NAME "/run/" DOSEMU_MIDI);
EXTERN char *dosemu_midi_in_path INIT("~/" LOCALDIR_BASE_NAME "/run/" DOSEMU_MIDI_IN);

#define    DOSEMU_USERS_FILE     dosemu_users_file_path
#define    DOSEMU_LOGLEVEL_FILE  dosemu_loglevel_file_path
#define    RUNDIR                dosemu_rundir_path
#define    LOCALDIR              dosemu_localdir_path
#define    KEYMAP_LOAD_BASE_PATH keymap_load_base_path
#define    KEYMAP_DIR            keymap_dir_path
#define    OWNER_LOCKS           owner_tty_locks
#define    PATH_LOCKD            tty_locks_dir_path
#define    NAME_LOCKF            tty_locks_name_path
#define    DOSEMU_MAP_PATH       dosemu_map_file_name
#define    DOSEMU_MIDI_PATH      dosemu_midi_path
#define    DOSEMU_MIDI_IN_PATH   dosemu_midi_in_path

#endif /* not __ASM__ */
])

AH_TEMPLATE([CONFIG_HOST],
[ Define the host for which Dosemu is configured ])

AH_TEMPLATE([CONFIG_TIME],
[ Define the configure time ])

AH_TEMPLATE(HAVE_XKB,
[Define this if you have the XKB extension])

AH_TEMPLATE([USE_DL_PLUGINS],
[ DEFINE this, if you want dynamically loaded plugins ])

AH_TEMPLATE([_DEBUG],
[ Define this for DEBUG info ])

AH_TEMPLATE([_GNU_SOURCE],
[ Define this if the GNU_SOURCE macro is necessary ])

AH_TEMPLATE([HAVE_SIGCONTEXT_SS],
[ Define this if struct sigcontext has .ss field ])

AH_BOTTOM([
#ifndef __ASM__
EXTERN char *dosemu_map_file_name INIT(0);
#endif

#include "plugin_config.h"

#endif /* CONFIG_H */
])

AC_OUTPUT

dnl Print some warnings (if neccessary)

if test "$GCC" != "yes" ; then
  AC_MSG_WARN( )
  AC_MSG_WARN(Your system doesn't seem to have the GNU C compiler (gcc).)
  AC_MSG_WARN(Trying to compile with $CC.)
  AC_MSG_WARN(If you are sure you have gcc, set the environment variable)
  AC_MSG_WARN(CC to the complete name of gcc. Examples:)
  AC_MSG_WARN(csh/tcsh: setenv CC /usr/local/bin/gcc)
  AC_MSG_WARN(sh/ksh/bash: CC=/usr/local/bin/gcc ; export CC)
  AC_MSG_WARN(Of course you could also include the gcc bin directory in)
  AC_MSG_WARN(your path.)
fi
