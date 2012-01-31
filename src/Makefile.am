AM_CPPFLAGS = -DSYSTEM_CFG=\"@sysconfdir@/greasy.conf\"

bin_PROGRAMS = greasy 
greasy_SOURCES = abstractengine.cpp abstractengine.h abstractschedulerengine.cpp abstractschedulerengine.h basicengine.cpp basicengine.h greasyconfig.cpp greasyconfig.h greasy.cpp greasylog.cpp greasylog.h greasyregex.cpp greasyregex.h greasytask.cpp greasytask.h greasytimer.cpp greasytimer.h greasyutils.h  

if MPI_ENGINE
greasy_SOURCES += mpiengine.cpp mpiengine.h
AM_CPPFLAGS += -DMPI_ENGINE
endif

if SLURM_ENGINE
greasy_SOURCES += slurmengine.cpp slurmengine.h 
AM_CPPFLAGS += -DSLURM_ENGINE
endif

if THREAD_ENGINE
greasy_SOURCES += threadengine.cpp threadengine.h 

if TBB_LOCAL
greasy_DEPENDENCIES = tbb 
tbb_root = $(CURDIR)/@my_tbb_root@
include @my_tbb_root@/build/common.inc	

AM_CPPFLAGS += -DTHREAD_ENGINE -I@my_tbb_root@/include 
AM_LDFLAGS  = -L@my_tbb_root@/build/$(tbb_build_prefix)_release -ltbb 

.PHONY: tbb
tbb:
	@echo "Compiling local version of TBB..."
	@cd @my_tbb_root@ && $(MAKE) $(AM_MAKEFLAGS) tbb && cd - 
else

AM_CPPFLAGS += -DTHREAD_ENGINE -I@tbb_include@
AM_LDFLAGS  = -R@tbb_libs@ -L@tbb_libs@ -ltbb 

endif 

endif


