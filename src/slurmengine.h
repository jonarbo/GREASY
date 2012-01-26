#ifndef SLURMENGINE_H
#define SLURMENGINE_H

#include <string>
#include <queue>
#include <map>
#include <limits.h>

#include "basicengine.h"

/**
  * This engine inherits AbstractSchedulerEngine, and implements a basic scheduler and launcher
  * for Greasy using Slurm's srun
  */
class SlurmEngine : public BasicEngine
{
  
public:

  /**
   * Constructor that adds the filename to process.
   * @param filename path to the task file.
   */
  SlurmEngine (const string& filename ); 
  

protected:
  
  /**
   * Run the command corresponding to a task using srun to spawn the process in the right place.
   * @param task The task to be executed.
   * @param worker The index of the worker in charge.
   * @return The exit status of the command.
   */  
  virtual int executeTask(GreasyTask *task, int worker);
  
};


#endif // SLURMENGINE_H
