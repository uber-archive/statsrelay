AC_INIT([statsrelay], m4_esyscmd([tr -d '\n' < VERSION]))
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])
AC_PROG_CC
AC_USE_SYSTEM_EXTENSIONS
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])
AC_CHECK_LIB([ev], [ev_run])
AC_CHECK_LIB([yaml], [yaml_parser_initialize])
AC_REVISION([m4_esyscmd_s([git describe --always])])
AC_OUTPUT
