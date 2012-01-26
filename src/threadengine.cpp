#include "threadengine.h"



ThreadEngine::ThreadEngine ( const string& filename) : AbstractEngine(filename){
  
  engineType="thread";

  // Set the number of workers
  if (config->keyExists("NWorkers"))
    nworkers = fromString(nworkers, config->getValue("NWorkers"));
  else getDefaultNWorkers();

}

void ThreadEngine::run(){
	log->record(GreasyLog::devel, "ThreadEngine::run", "Entering...");
	if (isReady()) runScheduler();
	log->record(GreasyLog::devel, "ThreadEngine::run", "Exiting...");
}

void ThreadEngine::init() {

  log->record(GreasyLog::devel, "ThreadEngine::init", "Entering...");

  AbstractEngine::init();

  log->record(GreasyLog::devel, "ThreadEngine::init", "Exiting...");

}

void ThreadEngine::writeRestartFile() {

  AbstractEngine::writeRestartFile();

}

void ThreadEngine::finalize() {

  AbstractEngine::finalize();

}

void ThreadEngine::dumpTasks(){
    AbstractEngine::dumpTasks();
}

void ThreadEngine::getDefaultNWorkers() {

    nworkers = tbb::task_scheduler_init::default_num_threads();
    log->record(GreasyLog::debug, "ThreadEngine::getDefaultNWorkers", "Default nworkers: " + toString(nworkers));

}

void ThreadEngine::runScheduler(){

  log->record(GreasyLog::devel, "ThreadEngine::runScheduler", "Entering...");

  // Dummy check: let's see if there is any worker...
  if (nworkers==0) {
    log->record(GreasyLog::error, "No workers found. Rerun greasy with more resources");
    return;
  }

  // initialize the task scheduler
  tbb::task_scheduler_init init(nworkers) ;

  // start counter
  tbb::tick_count globalStartTime = tbb::tick_count::now();

  log->record(GreasyLog::debug, "ThreadEngine::runScheduler", "Starting to launch tasks...");

  // fill the TBB task queue ... launch scheduler
  GreasyTBBTaskLauncher& a = *new(tbb::task::allocate_root()) GreasyTBBTaskLauncher(&taskMap,&validTasks,&revDepMap);
  tbb::task::spawn_root_and_wait(a);

  log->record(GreasyLog::debug, "ThreadEngine::runScheduler", "All tasks lauched");


  // end counter
  tbb::tick_count globalEndTime = tbb::tick_count::now();

  // compute total elapsed
  GreasyTimer::secsToTime( (globalEndTime-globalStartTime).seconds() );

  log->record(GreasyLog::devel, "ThreadEngine::runScheduler", "Exiting...");
}

/*************************************************************************************/
/***  TBB-related classes
/*************************************************************************************/

/***********************************/
/***   GreasyTBBTaskContinuation
/***********************************/

GreasyTBBTaskContinuation::GreasyTBBTaskContinuation (GreasyTask* gtask_, map<int,GreasyTask*> * taskMap_ , map<int,list<int> >* revDepMap_)
    : gtask(gtask_), taskMap(taskMap_),revDepMap(revDepMap_)
{;}

bool GreasyTBBTaskContinuation::taskEpilogue(){

    int maxRetries=0;
    bool retval = false;

    GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::taskEpilogue", "Entering...");

    if ( GreasyConfig::getInstance()->keyExists("MaxRetries")) fromString(maxRetries, GreasyConfig::getInstance()->getValue("MaxRetries"));

    if (gtask->getReturnCode() != 0) {
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

        updateDependencies(gtask);

      }
    } else {
      GreasyLog::getInstance()->record(GreasyLog::info,  "Task " + toString(gtask->getTaskId()) + " completed successfully. Elapsed: " +  GreasyTimer::secsToTime(gtask->getElapsedTime()));
      gtask->setTaskState(GreasyTask::completed);

      updateDependencies(gtask);

    }

    GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::taskEpilogue", "Exiting...");

    return retval;

}

void GreasyTBBTaskContinuation::updateDependencies(GreasyTask* child_){

    int taskId, state;
    GreasyTask* child = child_;
    list<int>::iterator it;
    GreasyLog* log =  GreasyLog::getInstance();

    log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Entering...");

    taskId = gtask->getTaskId();
    state  = gtask->getTaskState();

    log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Inspecting reverse deps for task " + toString(taskId));

    if ( revDepMap->find(taskId) == revDepMap->end() ){
        log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "The task "+ toString(taskId) + " does not have any other dependendant task. No update done.");
        log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "Exiting...");
        return;
    }

    for(it=(*revDepMap)[taskId].begin() ; it!=(*revDepMap)[taskId].end();it++ ) {

        // get the first child whose state is blocked
        log->record(GreasyLog::devel, "GreasyTBBTaskContinuation::updateDependencies", "State of task " + toString(taskId) + " should be 'blocked' ?= " + toString(child->getTaskState()));
        child = (*taskMap)[*it];


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

tbb::task* GreasyTBBTaskContinuation::execute(){

    GreasyLog::getInstance()->record(GreasyLog::devel, "GreasyTBBTaskContinuation::execute", "Entering...");

    // task is finished here ...
    if (!taskEpilogue() )
    {
        // task ended Ok ...

        // now see if it has dependants
        list<int> depList = gtask->getDependencies();
        list<int>::iterator it;
        GreasyTask* ngtask;
        int refcount = 0;
        for ( it=depList.begin(); it!=depList.end(); it++ )
        {
            ngtask = (*taskMap)[*it];
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

/***********************************/
/***   GreasyTBBTaskLauncher
/***********************************/

GreasyTBBTaskLauncher::GreasyTBBTaskLauncher( map<int,GreasyTask*> * taskMap_, set<int>* validTasks_ ,map<int,list<int> >*revDepMap_)
    :taskMap(taskMap_), validTasks(validTasks_), revDepMap(revDepMap_), reference_count(0)
{;}

tbb::task* GreasyTBBTaskLauncher::execute(){

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


/***********************************/
/***   GreasyTBBTask
/***********************************/

GreasyTBBTask::GreasyTBBTask(GreasyTask* gtask_)
    :gtask(gtask_)
{;}

tbb::task* GreasyTBBTask::execute(){
           // Overrides virtual function task::execute

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

