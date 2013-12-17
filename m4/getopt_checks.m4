AC_DEFUN([GETOPT_ERROR_ENABLE], [
  AC_MSG_ERROR([Please enable getopt in your compiler.])
])

AC_DEFUN([GETOPT_CHECK], [
  AC_SEARCH_LIBS([getopt], [], [], GETOPT_ERROR_ENABLE)
  AC_SEARCH_LIBS([optarg], [], [], GETOPT_ERROR_ENABLE)
  AC_SEARCH_LIBS([optind], [], [], GETOPT_ERROR_ENABLE)
  AC_SEARCH_LIBS([opterr], [], [], GETOPT_ERROR_ENABLE)
  AC_SEARCH_LIBS([optopt], [], [], GETOPT_ERROR_ENABLE)
])
