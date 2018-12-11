AC_INIT([nfcollect], [0.2])

# Safety checks in case user overwritten --srcdir
AC_CONFIG_SRCDIR([nfcollect.c])

# Store the auxiliary build tools (e.g., install-sh, config.sub, config.guess)
# in this dir (build-aux)
AC_CONFIG_AUX_DIR([build-aux])

# Init automake, and specify this program use relaxed structures.
# i.e. this program doesn't follow the gnu coding standards, and doesn't have
# ChangeLog, COPYING, AUTHORS, INSTALL, README etc. files.
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-xz subdir-objects])

AM_SILENT_RULES([yes])
AC_CONFIG_SRCDIR([bin/nfcollect.c])

AC_PROG_CC([clang])

# AC_DEFINE([DEBUG], [1], [Enable debug])
AC_ARG_ENABLE(debug,
AC_HELP_STRING([--enable-debug],[Enable debug mode (default is NO)]),
[case "${enableval}" in
	yes) debug=true ;;
	no) debug=false ;;
	*) AC_MSG_ERROR(bad_value ${enableval} for --enable-debug) ;;
esac],[debug=false])
if test x"$debug" = x"true"; then
   AC_DEFINE(DEBUG, 1, [debug])
fi

AC_CHECK_HEADERS(libnetfilter_log/libnetfilter_log.h)
AC_SEARCH_LIBS(nflog_open, netfilter_log)

AC_CHECK_HEADERS(pthread.h)
AC_SEARCH_LIBS(pthread_create, pthread)

AC_CHECK_HEADERS(sqlite3.h)
AC_SEARCH_LIBS(sqlite3_exec, sqlite3)

AC_CHECK_HEADERS(zstd.h)
AC_SEARCH_LIBS(ZSTD_compress, zstd)

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
