#ifndef BASICENGINE_H
#define BASICENGINE_H

#include <string>
#include <queue>
#include <map>
#include <limits.h>

#include "abstractschedulerengine.h"

/**
  * This engine inherits AbstractSchedulerEngine, and implements a basic scheduler and launcher
  * for Greasy in a single machine using system fork.
  */
class BasicEngine : public AbstractSchedulerEngine
{
  
public:

  /**
   * Constructor that adds the filename to process.
   * @param filename path to the task file.
   */
  BasicEngine (const string& filename ); 
  
  /**
   * Execute the engine. It is divided into 2 different parts, for master and workers.
   */
  virtual void run();


protected:
  
  /**
   * Allocate a task in a free worker, sending the command to it.
   * @param task A pointer to a GreasyTask object to allocate.
   */
  virtual void allocate(GreasyTask* task);
  
  /**
   * Wait for any worker to complete their tasks and retrieve
   * results.
   */
  virtual void waitForAnyWorker();
  
  map<pid_t,int> pidToWorker; /**<  Map to translate a pid to the corresponding worker. */
  map<int, GreasyTimer> workerTimers; /**<  Map of worker timers to know elapsed time of tasks. */
  char hostname[HOST_NAME_MAX]; ///< Cstring to hold the worker hostname.
};

#endif // BASICENGINE_H
