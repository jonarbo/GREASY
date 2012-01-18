##### Variables
SLURM   = 
INCDIR = -I. 
DEFINES = -DMPI_ENGINE
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

# DO NOT DELETE

greasylog.o: greasylog.h greasyutils.h
slurmengine.o: slurmengine.h greasyengine.h greasytask.h greasyconfig.h
slurmengine.o: greasyutils.h greasylog.h
greasyconfig.o: greasyconfig.h greasyutils.h greasyregex.h
mpiengine.o: mpiengine.h greasyengine.h greasytask.h greasyconfig.h
mpiengine.o: greasyutils.h greasylog.h
greasyengine.o: greasytask.h greasyconfig.h greasylog.h

