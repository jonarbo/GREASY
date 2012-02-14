AM_CPPFLAGS = -DSYSTEM_CFG=\"@sysconfdir@/greasy.conf\"

bin_PROGRAMS = greasybin
greasybin_SOURCES = abstractengine.cpp abstractengine.h abstractschedulerengine.cpp abstractschedulerengine.h basicengine.cpp basicengine.h greasyconfig.cpp greasyconfig.h greasy.cpp greasylog.cpp greasylog.h greasyregex.cpp greasyregex.h greasytask.cpp greasytask.h greasytimer.cpp greasytimer.h greasyutils.h  

if MPI_ENGINE
greasybin_SOURCES += mpiengine.cpp mpiengine.h
AM_CPPFLAGS += -DMPI_ENGINE
endif

# if SLURM_ENGINE
# greasybin_SOURCES += slurmengine.cpp slurmengine.h 
# AM_CPPFLAGS += -DSLURM_ENGINE
# endif

if THREAD_ENGINE
greasybin_SOURCES += threadengine.cpp threadengine.h 

if TBB_LOCAL
greasybin_DEPENDENCIES = tbb 
tbb_root = $(CURDIR)/@my_tbb_root@
include @my_tbb_root@/build/common.inc	

AM_CPPFLAGS  += -DTHREAD_ENGINE -I@my_tbb_root@/include 
AM_LDFLAGS    = -L@my_tbb_root@/build/$(tbb_build_prefix)_release -ltbb 

ACLOCAL_AMFLAGS = -I m4

#lib_LTLIBRARIES = libtbbproxy.la
#libtbbproxy_la_SOURCES  = dummy.cpp
#libtbb_la_LIBADD   = -ltbb 
#libtbb_la_LDFLAGS =  -L@my_tbb_root@/build/$(tbb_build_prefix)_release -ltbb
#greasybin_LDADD	   =  libtbb.la
#greasybin_LDFLAGS  =  -ltbbproxy
#greasybin_LDFLAGS  = -L@my_tbb_root@/build/$(tbb_build_prefix)_release -ltbb 
#greasybin_LDADD	   = libtbb.so.2
#libtbb.la

#install-exec-local:
#	test -z @greasy_libdir@ || $(MKDIR_P) @greasy_libdir@
#	$(INSTALL_PROGRAM) @my_tbb_root@/build/$(tbb_build_prefix)_release/libtbb.so  "@greasy_libdir@"
	
.PHONY: tbb 

tbb:
	@echo "Compiling local version of TBB..."
	@cd @my_tbb_root@ && $(MAKE) $(AM_MAKEFLAGS) tbb && cd - 
else

AM_CPPFLAGS += -DTHREAD_ENGINE -I@tbb_include@
AM_LDFLAGS  = -R@tbb_libs@ -L@tbb_libs@ -ltbb 

endif 

endif


