#include "slurmengine.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

SlurmEngine::SlurmEngine ( const string& filename) : BasicEngine(filename){
  
  engineType="slurm";
  
}

int SlurmEngine::executeTask(GreasyTask *task, int worker) {
  
  string command = "srun -n1 " + task->getCommand();
  log->record(GreasyLog::devel,  "SlurmEngine::allocate[" + toString(worker) +"]", "Task " 
		      + toString(task->getTaskId()) + " command: " + command);
  return system(command.c_str());
  
}



