AC_INIT([xlmon], [1.0], [philip.wernersbach@gmail.com])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

AC_PROG_CC_C99

AC_CHECK_HEADERS([signal.h], [], [
  AC_MSG_ERROR(Please install your Operating System development headers.)
])

AC_CHECK_HEADERS([libxl_utils.h], [], XEN_ERROR_INSTALL_DEV_FILES)

OS_SEARCH_LIBS([sleep])
OS_SEARCH_LIBS([geteuid])
OS_SEARCH_LIBS([signal])

GETOPT_CHECK

XEN_SEARCH_LIBS([libxl_ctx_free])
XEN_SEARCH_LIBS([xtl_logger_destroy])
XEN_SEARCH_LIBS([libxl_name_to_domid])
XEN_SEARCH_LIBS([xtl_createlogger_stdiostream])
XEN_SEARCH_LIBS([libxl_ctx_alloc])
XEN_SEARCH_LIBS([libxl_domain_info])
XEN_SEARCH_LIBS([libxl_domain_destroy])
XEN_SEARCH_LIBS([libxl_domain_shutdown])
XEN_SEARCH_LIBS([libxl_send_trigger])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
