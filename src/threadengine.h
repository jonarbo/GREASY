#ifndef THREADENGINE_H
#define THREADENGINE_H

#include <queue>
#include "abstractengine.h"

#include "tbb/tbb.h"
#include "tbb/task.h"
//#include "tbb/tick_count.h"
#include "tbb/task_scheduler_init.h"

/**
  * This engine inherits AbstractEngine, and implements a Threaded scheduler for Greasy.
  * There is a similar engine called "ForkEngine" that works similarly to this except that
  * it calls "forks" instead of using threads.
  * This class inherits from "AbstractEngine" and not from "AbstractSchedulerEngine"  because
  * here we don't need the scheduler  functionality since TBB has its own scheduler.
  */
class ThreadEngine : public AbstractEngine
{
public:

  /**
   * Constructor that adds the filename to process.
   * @param filename path to the task file.
   */
  ThreadEngine (const string& filename );

 /**
  * Execute the engine. It is divided into 2 different parts, for master and workers.
  */
 virtual void run();


  /**
   * reimplementation of the init() method adding the workers init code.
   */
  virtual void init();

  /**
   * Methdod that  should provide all the finalization
   * and clean-up code of the engine. An implementation is provided to do the basic cleanup
   * of abstract engine, and MUST be called from the subclass if reimplemented.
   */
  virtual void finalize();

  /**
   * Base method to write the restart file. It creates a file named taskFile.rst, and adds to it
   * all the tasks that didn't complete successfully (they were waiting, running, failed or cancelled).
   * It will also add invalid tasks, but commented out. All dependencies will be recorded along each task
   * with the indexes updated to the new line numbering.
   */
  virtual void writeRestartFile();

  /**
   * Method that should print the contents of the taskMap,
   * calling dumpTaskMap(). This method is only for debugging purposes
   */
  virtual void dumpTasks();

protected:

  /**
   * Perform the scheduling of tasks to workers
   */
  void runScheduler();

  /**
   * Get default number of workers according to the cpus available in the computer.
   */
  virtual void getDefaultNWorkers();

};

/*************************************************************************************/
/***  TBB-related classes
/*************************************************************************************/

// forward declarations
class GreasyTBBTaskContinuation;
class GreasyTBBTask;
class GreasyTBBTaskLauncher;

/**
  * This class wraps a task as seen from the TBB task scheduler.
  * It is a 'continuation' class... see TBB documentation to get more info on it.
  * It basically allows a parent tasks to continue while the childs are still running
  * to increase efficiency.
  */
class GreasyTBBTaskContinuation: public tbb::task{

public:

    /**
      * Constructor
      **/
    GreasyTBBTaskContinuation(GreasyTask* gtask_, map<int,GreasyTask*> * taskMap_ , map<int,list<int> >* revDepMap_);

    /**
     * All the checks after a task finishes are done here, updating task and engine metadata.
     */
    bool taskEpilogue();

    /**
     * Update all the tasks depending for the current task which has finished.
     *
     */
    void updateDependencies(GreasyTask* child_ );

    /**
      * Core function that should be implemented in thr TBB task model.
      *
      */
    task* execute();

protected:

    GreasyTask* const gtask;
    map<int,GreasyTask*> * const taskMap;
    map<int,list<int> >* const revDepMap;

};

/**
  * This class wraps a task as seen from the TBB task scheduler.
  * This is the class that is responsible start the scheduler and launch
  * all the tasks.
  */
class GreasyTBBTaskLauncher: public tbb::task{

public:

    /**
      * Constructor
      **/
    GreasyTBBTaskLauncher( map<int,GreasyTask*> * taskMap_, set<int>* validTasks_ ,map<int,list<int> >*revDepMap_ );

    /**
      * Core function that should be implemented in the TBB task model.
      */
    task* execute();

protected:

    map<int,GreasyTask*> * const taskMap;
    map<int,list<int> >* const revDepMap;
    set<int>* const validTasks;
    int reference_count;

};

/**
  * This class wraps a task as seen from the TBB task scheduler.
  * This is the class that is responsible to spawn the child tasks,
  * creating a tree-like structure as recommended in the TBB documentation.
  */
class GreasyTBBTask: public tbb::task {

public:

    /**
      * Constructor
      **/
    GreasyTBBTask(GreasyTask* gtask_ );

    /**
      * Core function that should be implemented in thr TBB task model
      */
    task* execute();

protected:

    GreasyTask* const gtask;
    GreasyTimer timer;
};


#endif // THREADENGINE_H
