#ifndef THREADENGINE_H
#define THREADENGINE_H

#include <queue>
#include "abstractengine.h"

#include "tbb/tbb.h"

// forward declarations
class GreasyTBBTaskContinuation;
class GreasyTBBTask;
class GreasyTBBTaskLauncher;

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

/**
  * This class wraps a task as seen from the TBB task scheduler.
  * It is a 'continuation' class... see TBB documentation to get more info on it.
  * It basically allows a parent tasks to continue while the childs are still running
  * to increase efficiency.
  */
class GreasyTBBTaskContinuation: public tbb::task{

    GreasyTask* const gtask;
    map<int,GreasyTask*> * const taskMap;
    map<int,list<int> >* const revDepMap;

    /**
      * Constructor
      **/
    GreasyTBBTask(GreasyTask* gtask_, map<int,GreasyTask*> * taskMap_ , map<int,list<int> >* revDepMap_) :
        gtask(gtask_), taskMap(taskMap_),revDepMap(revDepMap_)
    {}

    /**
     * All the checks after a task finishes are done here, updating task and engine metadata.
     */
    bool taskEpilogue(){

        int maxRetries=0;
        bool retval = false;

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::taskEpilogue", "Entering...");

        if ( GreasyConfig::getInstance()->keyExists("MaxRetries")) fromString(maxRetries, config->getValue("MaxRetries"));

        if (task->getReturnCode() != 0) {
          GreasyLog::getInstance()->record(GreasyLog::error,  "Task " + toString(gtask->getTaskId()) + " failed with exit code " + toString(gtask->getReturnCode()) +". Elapsed: " + GreasyTimer::secsToTime(gtask->getElapsedTime()));

          // Task failed, let's retry if we need to
          if ((maxRetries > 0) && (gtask->getRetries() < maxRetries)) {
            GreasyLog::getInstance()->record(GreasyLog::warning,  "Retry "+ toString(gtask->getRetries()) + "/" + toString(maxRetries) + " of task " + toString(gtask->getTaskId()));
            gtask->addRetryAttempt();

            // allocate task again...

            // first create a 'continuation' task to free this thread when finished
            GreasyTBBTaskContinuation& c = *new(allocate_continuation()) GreasyTBBTaskContinuation(gtask,taskMap,revDepMap) ;
            // can be allocated inmediatelly, so create child
            GreasyTBBTask& a = *new( c.allocate_child() ) GreasyTBBTask( gtask );

            set_ref_count( 1 );

            spawn(a);

            retval= true;

          } else {
            gtask->setTaskState(GreasyTask::failed);

            updateDependencies();

          }
        } else {
          GreasyLog::getInstance()->record(GreasyLog::info,  "Task " + toString(gtask->getTaskId()) + " completed successfully. Elapsed: " +  GreasyTimer::secsToTime(gtask->getElapsedTime()));
          gtask->setTaskState(GreasyTask::completed);

          updateDependencies();

        }

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::taskEpilogue", "Exiting...");

        return retval;
    }

    /**
     * Update all the tasks depending for the current task which has finished.
     *
     */
    void updateDependencies(GreasyTask* child_ = gtask){

        int taskId, state;
        GreasyTask* child = child_;
        list<int>::iterator it;
        GreasyLog* log =  GreasyLog::getInstance();

        log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Entering...");

        taskId = gtask->getTaskId();
        state  = gtask->getTaskState();

        log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Inspecting reverse deps for task " + toString(taskId));

        if ( revDepMap.find(taskId) == revDepMap.end() ){
            log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "The task "+ toString(taskId) + " does not have any other dependendant task. No update done.");
            log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Exiting...");
            return;
        }

        for(it=revDepMap[taskId].begin() ; it!=revDepMap[taskId].end();it++ ) {

            // get the first child whose state is blocked
            log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "State of task " + toString(taskId) + " should be 'blocked' ?= " + toString(child->getTaskState()));
            child = taskMap[*it];


          if (state == GreasyTask::completed) {
            log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Remove dependency " + toString(taskId) + " from task " + toString(child->getTaskId()));
            child->removeDependency(taskId);
            if (!child->hasDependencies()) {
              log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Remove blocked state and queue task for execution");
              child->setTaskState(GreasyTask::waiting);
            } else {
              log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "The task still has dependencies, so leave it blocked");
            }
          }
          else if ((state == GreasyTask::failed)||(state == GreasyTask::cancelled)) {
            log->record(GreasyLog::warning,  "Cancelling task " + toString(child->getTaskId()) + " because of task " + toString(taskId) + " failure");
            log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Parent failed: cancelling task and removing it from blocked");
            child->setTaskState(GreasyTask::cancelled);

            updateDependencies(child);
          }
        }

        log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Exiting...");
    }


    /**
      * Core function that should be implemented in thr TBB task model.
      *
      */
    task* execute()
    {
        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::execute", "Entering...");

        // task is finished here ...
        if (!taskEpilogue() )
        {
            // task ended Ok ...

            // now see if it has dependants
            list<int> depList = gtask->getDependencies();
            set<int>::iterator it;
            GreasyTask* ngtask;
            int refcount = 0;
            for ( it=depList->begin(); it!=depList->end(); it++ )
            {
                ngtask = taskMap[*it];
                if ( ngtask->isWaiting() ){

                    // first create a 'continuation' task to free this thread when finished
                    GreasyTBBTaskContinuation& c = *new(allocate_continuation()) GreasyTBBTaskContinuation(ngtask,taskMap,revDepMap) ;

                    // can be allocated inmediatelly, so create child
                    GreasyTBBTask& a = *new( c.allocate_child() ) GreasyTBBTask( ngtask );

                    set_ref_count( ++refcount );

                    GreasyLog::getInstance()->record(GreasyLog::debug, "GreasyTBBTaskLauncher::execute", "Allocating task ID = " + toString(gtask->getTaskId()) );

                    // Start running the task.
                    spawn( a );
                 }

            }
        }

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::execute", "Exiting...");
        return NULL;
    }
};

/**
  * This class wraps a task as seen from the TBB task scheduler.
  * This is the class that is responsible start the scheduler and launch
  * all the tasks.
  */
class GreasyTBBTaskLauncher: public tbb::task{

    map<int,GreasyTask*> * const taskMap;
    map<int,list<int> >* const revDepMap;
    set<int>* const validTasks;
    int reference_count;

    /**
      * Constructor
      **/
    GreasyTBBTaskLauncher( map<int,GreasyTask*> * taskMap_, set<int>* validTasks_ ,map<int,list<int> >*revDepMap_ ) :
        taskMap(taskMap_), validTasks(validTasks_), revDepMap(revDepMap_), reference_count(0)
    {
    }

    /**
      * Core function that should be implemented in the TBB task model.
      */
    task* execute()
    {

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskLauncher::execute", "Entering...");

        set<int>::iterator it;
        GreasyTask* gtask  = NULL;

        for ( it=validTasks->begin(); it!=validTasks->end(); it++ ) {
          gtask = (*taskMap)[*it];
          if ( gtask->isWaiting() ){

              // first create a 'continuation' task to free this thread when finished
              GreasyTBBTaskContinuation& c = *new(allocate_continuation()) GreasyTBBTaskContinuation(gtask,taskMap,revDepMap) ;

              // can be allocated inmediatelly, so create child
              GreasyTBBTask& a = *new( c.allocate_child() ) GreasyTBBTask( gtask );

              set_ref_count( ++reference_count );

              GreasyLog::getInstance()->record(GreasyLog::debug, "GreasyTBBTaskLauncher::execute", "Allocating task ID = " + toString(gtask->getTaskId()) );

              // Start running the task.
              spawn( a );
           }
        }

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskLauncher::execute", "Exiting...");
        return NULL;
    }

};

/**
  * This class wraps a task as seen from the TBB task scheduler.
  * This is the class that is responsible to spawn the child tasks,
  * creating a tree-like structure as recommended in the TBB documentation.
  */
class GreasyTBBTask: public tbb::task {

public:

    GreasyTask* const gtask;
    tbb::tick_count start,end;

    /**
      * Constructor
      **/
    GreasyTBBTask(GreasyTask* gtask_ ) :
        gtask(gtask_)
    {}

    /**
      * Core function that should be implemented in thr TBB task model
      */
    task* execute() { // Overrides virtual function task::execute

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTask::execute", "Entering...");

        GreasyLog::getInstance()->record(GreasyLog::debug, "GreasyTBBTask::execute", "Executing command: " + gtask->getCommand());

        start.now();
        int retcode = system(gtask->getCommand().c_str());
        end.now();

        gtask->setReturnCode(retcode);
        gtask->setElapsedTime( (end-start).seconds() );

        GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTask::execute", "Exiting...");

        return NULL;
    }
};


#endif // THREADENGINE_H
