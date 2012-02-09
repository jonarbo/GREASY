#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.65])
AC_INIT([greasy], [2.0], [support@bsc.es])
AM_INIT_AUTOMAKE(greasy,2.0)
AC_CONFIG_SRCDIR([src/greasylog.h])
AC_CONFIG_HEADERS([config.h])

AC_SUBST(greasy_engine,[basic])

# Checks for programs.
AC_PROG_CC

AC_PROG_LIBTOOL
#LT_INIT
# Checks for MPI-ENGINE

AC_ARG_ENABLE(mpi-engine,
  [  --enable-mpi-engine			enable MPI engine (default=no)],
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
  
  AC_CHECK_PROGS(machine_mpirun,[mnrun srun mpirun mpiexec],[no-mpirun-found])
  if test x$machine_mpirun = xno-mpirun-found; then
    AC_MSG_ERROR([Fatal Error: no MPI executer found])
    exit;
  fi

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

# Checks for Basic engine
AC_CHECK_PROGS(basic_spawn, [srun ssh],[no])
if test x$basic_spawn != xno; then
  if test x$basic_spawn = xsrun; then 
    AC_SUBST(remotemethod,[srun])
  else
    if test x$basic_spawn = xssh; then 
      AC_SUBST(remotemethod,[ssh])
    else
      AC_SUBST(remotemethod,[none])
    fi
  fi
fi


# Checks for SLURM-ENGINE

AC_ARG_ENABLE(slurm-engine,
  [  --enable-slurm-engine			enable SLURM engine (Not yet implemented)],
  [slurm_engine=${enableval}],
  [slurm_engine=no])

# WARNING: implement slurm engine before enabling in Makefile.am
AM_CONDITIONAL(SLURM_ENGINE, test x$slurm_engine = xyes)

if test x$slurm_engine = xyes; then

  AC_CHECK_PROG(srun_present,[srun],[yes],[no])
  if test x$srun_present = xno; then
    AC_MSG_ERROR([Fatal Error: srun command not found])
    exit;
  fi

# TODO: implement slurm engine before enabling
#   AC_SUBST(greasy_engine,[slurm])

fi

# Checks for THREAD-ENGINE

AC_ARG_ENABLE(thread-engine,
  [  --enable-thread-engine		enable THREAD engine. This option will compile a local distribution of TBB by default. If --with-tbb is set it will used the specified installation of TBB.],
  [thread_engine=${enableval}],
  [thread_engine=no])

# check for threads TBB
AM_CONDITIONAL(THREAD_ENGINE, test x$thread_engine = xyes)

# check for local TBB
AC_ARG_WITH([tbb],
   [AS_HELP_STRING([--with-tbb], [Path to the system TBB installation. Otherwise will use the TBB version distributed with GREASY])], 
   [tbb=$withval], 
   [tbb=no])


AM_CONDITIONAL(TBB_LOCAL, false )
if test x$thread_engine = xyes; then

  AC_DEFINE(THREAD_ENGINE, 1, Thread Engine enabled with TBB)

   if test x$tbb != xno; then 
	# use system TBB
	echo "Using system TBB in $tbb"
	AC_SUBST(tbb_include,[$tbb/include])
	AC_SUBST(tbb_libs,[$tbb/libs])
   else
	echo "Using TBB provided by GREASY...will compile TBB sources"
	AC_SUBST(my_tbb_root,[3rdparty/tbb])
	AM_CONDITIONAL(TBB_LOCAL, true )
   fi
else
   if test x$tbb != xno; then 
	echo "Skipping '--with-tbb' option ... if you want to use TBB you must '--enable-thread-engine'"
   fi
fi 

AC_SUBST(greasy_bindir,[$prefix/bin])
AC_SUBST(greasy_etcdir,[$prefix/etc])
AC_SUBST(greasy_libdir,[$prefix/lib])

AC_OUTPUT(Makefile src/Makefile etc/Makefile etc/greasy.conf doc/Makefile example/Makefile example/bsc_greasy.job bin/Makefile)
