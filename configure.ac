#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([greasy], [2.0], [support@bsc.es])
AM_INIT_AUTOMAKE(greasy,2.0)
AC_CONFIG_SRCDIR([src/greasylog.h])
AC_CONFIG_HEADERS([config.h])

AC_SUBST(greasy_engine,[basic])

# Checks for programs.
AC_PROG_CC

# Check for mpi
AC_ARG_ENABLE(mpi-engine,
  [  --enable-mpi-engine    enable MPI engine (default=no)],
  [mpi_engine=${enableval}],
  [mpi_engine=no])

AM_CONDITIONAL(MPI_ENGINE, test x$mpi_engine = xyes)

if test x$mpi_engine = xyes; then
  AC_DEFINE(MPI_ENGINE, 1, MPI Engine enabled)
  AC_CHECK_PROG(mpicxx_present, mpicxx, [yes], [no])
  if test x$mpicxx_present = xno; then
    AC_CHECK_PROG(mpicxx_present, mpiCC, [yes], [no])
    if test x$mpicxx_present = xno; then
      AC_MSG_ERROR([Fatal Error: no mpi compiler found])
      exit;
    else
      CXX=mpiCC
    fi
  else
    CXX=mpicxx
  fi
  AC_PROG_CXX
  AC_SUBST(greasy_engine,[mpi])
else
  AC_PROG_CXX
fi

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([limits.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_CHECK_FUNCS([getcwd gethostname gettimeofday regcomp])

# Checks for other command line options


AC_ARG_ENABLE(slurm-engine,
  [  --enable-slurm-engine    enable SLURM engine (not implemented yet)],
  [slurm_engine=${enableval}],
  [slurm_engine=no])

AM_CONDITIONAL(SLURM_ENGINE, test x$slurm_engine = xyes)

#TODO: Implement checks for SLURM

AC_ARG_ENABLE(thread-engine,
  [  --enable-thread-engine    enable THREAD engine (not implemented yet)],
  [thread_engine=${enableval}],
  [thread_engine=no])

AM_CONDITIONAL(THREAD_ENGINE, test x$thread_engine = xyes)

#AC_DEFINE(SYSTEM_CFG, , System Config file)

AC_OUTPUT(Makefile src/Makefile etc/Makefile etc/greasy.conf doc/Makefile example/Makefile)
