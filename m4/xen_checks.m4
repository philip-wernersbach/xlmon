AC_DEFUN([XEN_ERROR_INSTALL_DEV_FILES], [
  AC_MSG_ERROR([Please install the Xen and libxenlight development files.])
])

AC_DEFUN([XEN_SEARCH_LIBS], [
  AC_SEARCH_LIBS($1, [xlutil xenctrl xenlight], [], XEN_ERROR_INSTALL_DEV_FILES)
])
