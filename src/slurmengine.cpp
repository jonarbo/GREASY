#include "slurmengine.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

SlurmEngine::SlurmEngine ( const string& filename) : AbstractEngine(filename){
  
  engineType="slurm";
  
}




