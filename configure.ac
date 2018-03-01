AC_INIT([nfcollect], [1.0])

# Safety checks in case user overwritten --srcdir
AC_CONFIG_SRCDIR([nfcollect.c])

# Store the auxiliary build tools (e.g., install-sh, config.sub, config.guess)
# in this dir (build-aux)
AC_CONFIG_AUX_DIR([build-aux])

# Init automake, and specify this program use relaxed structures.
# i.e. this program doesn't follow the gnu coding standards, and doesn't have
# ChangeLog, COPYING, AUTHORS, INSTALL, README etc. files.
AM_INIT_AUTOMAKE([-Wall -Werror foreign dist-xz])

AC_PROG_CC

AC_CHECK_HEADERS(libnetfilter_log/libnetfilter_log.h)
AC_SEARCH_LIBS(nflog_open, netfilter_log)

AC_CHECK_HEADERS(pthread.h)
AC_SEARCH_LIBS(pthread_create, pthread)

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
