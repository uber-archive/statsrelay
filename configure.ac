AC_INIT([statsrelay], [1.1.6])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_CHECK_LIB([crypto], [EVP_MD_CTX_init])
AC_CHECK_LIB([ev], [ev_run])
AC_CHECK_LIB([m], [floor])
AC_CHECK_LIB([glib-2.0], [g_hash_table_new])
PKG_CHECK_MODULES([GLIB], [glib-2.0])
AC_REVISION([m4_esyscmd_s([git describe --always])])
AC_OUTPUT
