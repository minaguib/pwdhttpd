#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT([pwdhttpd], [0.1], [mina.pwdhttpd@naguib.ca])
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR([config.h.in])
AM_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_CHECK_HEADERS([event.h arpa/inet.h fcntl.h netinet/in.h stdlib.h string.h sys/param.h sys/socket.h sys/time.h unistd.h dns_sd.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_MALLOC
AC_FUNC_SETVBUF_REVERSED
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_CHECK_FUNCS([bzero getcwd inet_ntoa memset realpath socket strchr strdup strstr strtol])
AC_CHECK_LIB(event, event_init, [], [
	echo "Error! You need to have libevent around."
	echo "If it's installed in a nonstandard location, try re-running configure with"
	echo "CFLAGS='-I/includedir -L/libdir'"
	exit -1
	])
AC_CHECK_LIB(event, bufferevent_new, [], [
	echo "Error! Your libevent is too old."
	echo "While you appear to have a working copy of libevent, it appears to be too old"
	echo "Please upgrade it to the latest version (or at least one that has the bufferevent_* functions)"
	exit -1
	])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT
