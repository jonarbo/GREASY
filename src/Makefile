##### Variables
PREFIX?=../
SLURM   =
INCDIR = -I.
DEFINES = -DMPI_ENGINE -DSYSTEM_CFG="\"$(PREFIX)/etc/greasy.conf\""
CXXFLAGS = -O2 -g $(INCDIR) $(DEFINES)
LDFLAGS = 
CXX = mpicxx

##### Files

SOURCES = $(wildcard *.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(wildcard *.cpp))
TARGET = greasy-bin

all: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) $(LDFLAGS) -o $(TARGET)

depend:
	@makedepend $(INCDIR) -Y. -m $(SOURCES) #2> /dev/null

clean:
	@rm -f *.o *.bak *~ *% core* 

install: all
	cp greasy $(PREFIX)/bin

# DO NOT DELETE

greasylog.o: greasylog.h greasyutils.h greasytimer.h
mpiengine.o: mpiengine.h abstractschedulerengine.h abstractengine.h greasyconfig.h
mpiengine.o: greasyutils.h greasylog.h greasytimer.h greasytask.h
greasy.o: greasylog.h greasyutils.h greasyconfig.h abstractengine.h
greasy.o: greasytimer.h greasytask.h
basicengine.o: abstractschedulerengine.h abstractengine.h greasyconfig.h greasyutils.h
basicengine.o: greasylog.h greasytimer.h greasytask.h
greasyregex.o: greasyregex.h greasyutils.h
slurmengine.o: slurmengine.h abstractengine.h greasyconfig.h greasyutils.h
slurmengine.o: greasylog.h greasytimer.h greasytask.h
greasyengine.o: abstractengine.h greasyconfig.h greasyutils.h greasylog.h
greasyengine.o: greasytimer.h greasytask.h greasyregex.h
greasyconfig.o: greasyconfig.h greasyutils.h greasyregex.h
greasytimer.o: greasytimer.h
greasytask.o: greasytask.h greasyutils.h greasyregex.h
