#include "threadengine.h"
#include "tbb/tick_count.h"
#include "tbb/task_scheduler_init.h"

ThreadEngine::ThreadEngine ( const string& filename) : AbstractEngine(filename){
  
  engineType="thread";

  // Set the number of workers
  if (config->keyExists("NWorkers"))
    nworkers = fromString(nworkers, config->getValue("NWorkers"));
  else getDefaultNWorkers();

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

ThreadEngine::runScheduler(){

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
  GreasyTBBTaskLauncher& a = *new(task::allocate_root()) GreasyTBBTaskLauncher(&taskMap,&validTasks,&revDepMap);
  task::spawn_root_and_wait(a);

  log->record(GreasyLog::debug, "ThreadEngine::runScheduler", "All tasks lauched");


  // end counter
  tbb::tick_count globalEndTime = tbb::tick_count::now();

  // compute total elapsed
  GreasyTimer::secsToTime( (globalEndTime-globalStartTime).seconds() );

  log->record(GreasyLog::devel, "ThreadEngine::runScheduler", "Exiting...");
}
