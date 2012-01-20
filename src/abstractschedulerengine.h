#ifndef ABSTRACTSCHEDULERENGINE_H
#define ABSTRACTSCHEDULERENGINE_H

#include <string>
#include <queue>

#include "abstractengine.h"

/**
  * This engine inherits AbstractEngine, and implements a basic scheduler for Greasy
  * to be run in a single node.
  * 
  */
class AbstractSchedulerEngine : public AbstractEngine
{
  
public:

  /**
   * Constructor that adds the filename to process.
   * @param filename path to the task file.
   */
  AbstractSchedulerEngine (const string& filename );
  
  /**
   * reimplementation of the init() method adding the workers init code.
   */
  virtual void init();

protected:
  
  /**
   * Perform the scheduling of tasks to workers
   */
  virtual void runScheduler();
  
  /**
   * Allocate a task in a free worker, sending the command to it.
   * @param task A pointer to a GreasyTask object to allocate.
   */
  virtual void allocate(GreasyTask* task) = 0;
  
  /**
   * Wait for any worker to complete their tasks and retrieve
   * results. It MUST be implemented in subclasses.
   */
  virtual void waitForAnyWorker() = 0;
  
  /**
   * Update all the tasks depending from the parent task which has finished.
   * @param parent A pointer to a GreasyTask object that finished.
   */
  virtual void updateDependencies(GreasyTask* parent);
  
  /**
   * Get default number of workers according to the cpus available in the computer.
   */
  virtual void getDefaultNWorkers();
  
  
  map <int,int> taskAssignation; ///<  Map that holds the task assignation to workers.
				 ///< worker -> taskId
  queue <int> freeWorkers; ///< The queue of free worker ids, from where the candidates
			   ///< to run a task will be taken.
  queue <GreasyTask*> taskQueue; ///< The queue of tasks to be executed.
  set <GreasyTask*> blockedTasks; ///< The set of blocked tasks.

};

#endif // ABSTRACTSCHEDULERENGINE_H
