###### DEFINE HERE THE KIND OF SCHEDULER TO COMPILE
ENGINE = MPI
#ENGINE = BASIC
#ENGINE = THREAD
#ENGINE = SLURM

##### Variables
PREFIX?=../
INCDIR = -I.

DEFINES = -D${ENGINE}_ENGINE -DSYSTEM_CFG="\"$(PREFIX)/etc/greasy.conf\""
CXX = mpicxx
CXXFLAGS = -O2 -g $(INCDIR) $(DEFINES)
LDFLAGS = 
LIBS = 

##### Files
SOURCES = $(wildcard *.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(wildcard *.cpp))
TARGET = greasy-bin

##### Filter out inncessary objects to avoid compiling those files
ifeq ($(ENGINE), MPI) 
OBJECTS := $(filter-out basicengine.o threadengine.o slurmengine.o ,$(OBJECTS))
endif
ifeq ($(ENGINE), BASIC) 
OBJECTS := $(filter-out mpiengine.o threadengine.o slurmengine.o ,$(OBJECTS))
endif
ifeq ($(ENGINE), THREAD) 
OBJECTS := $(filter-out basicengine.o mpiengine.o slurmengine.o ,$(OBJECTS))
endif
ifeq ($(ENGINE), SLURM) 
OBJECTS := $(filter-out basicengine.o mpiengine.o threadengine.o ,$(OBJECTS))
endif

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) -o $(TARGET) $^ $(LDFLAGS)  $(LIBS) 

depend:
	@makedepend $(INCDIR) -Y. -m $(SOURCES) #2> /dev/null

.PHONY: clean

clean:
	@rm -f *.o *.bak *~ *% core* 

.PHONY: install

install: all
	cp greasy $(PREFIX)/bin

# DO NOT DELETE: dependencies here

abstractengine.o: abstractengine.h greasyconfig.h greasyutils.h greasylog.h Makefile
abstractengine.o: greasytimer.h greasytask.h greasyregex.h
abstractschedulerengine.o: abstractschedulerengine.h abstractengine.h
abstractschedulerengine.o: greasyconfig.h greasyutils.h greasylog.h
abstractschedulerengine.o: greasytimer.h greasytask.h
basicengine.o: basicengine.h abstractschedulerengine.h abstractengine.h
basicengine.o: greasyconfig.h greasyutils.h greasylog.h greasytimer.h
basicengine.o: greasytask.h
greasyconfig.o: greasyconfig.h greasyutils.h greasyregex.h
greasy.o: greasylog.h greasyutils.h greasyconfig.h abstractengine.h
greasy.o: greasytimer.h greasytask.h
greasylog.o: greasylog.h greasyutils.h greasytimer.h
greasyregex.o: greasyregex.h greasyutils.h
greasytask.o: greasytask.h greasyutils.h greasyregex.h
greasytimer.o: greasytimer.h
mpiengine.o: mpiengine.h abstractschedulerengine.h abstractengine.h
mpiengine.o: greasyconfig.h greasyutils.h greasylog.h greasytimer.h
mpiengine.o: greasytask.h
slurmengine.o: slurmengine.h abstractengine.h greasyconfig.h greasyutils.h
slurmengine.o: greasylog.h greasytimer.h greasytask.h
