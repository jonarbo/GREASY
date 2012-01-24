AM_CPPFLAGS = -DSYSTEM_CFG=\"@sysconfdir@/greasy.conf\"

bin_PROGRAMS = greasy
greasy_SOURCES = abstractengine.cpp abstractengine.h abstractschedulerengine.cpp abstractschedulerengine.h basicengine.cpp basicengine.h greasyconfig.cpp greasyconfig.h greasy.cpp greasylog.cpp greasylog.h greasyregex.cpp greasyregex.h greasytask.cpp greasytask.h greasytimer.cpp greasytimer.h greasyutils.h  

if MPI_ENGINE
greasy_SOURCES += mpiengine.cpp mpiengine.h
endif
if SLURM_ENGINE
greasy_SOURCES += slurmengine.cpp slurmengine.h 
endif
if THREAD_ENGINE
greasy_SOURCES += threadengine.cpp threadengine.h 
endif