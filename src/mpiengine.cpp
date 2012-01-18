#include "mpiengine.h"
#include <csignal>
#include <cstdlib>
#include <cstring>


typedef struct {
    int workerStatus;
    int retcode;
    unsigned long elapsed;
    char hostname[MPI_MAX_PROCESSOR_NAME] ;
} reportStruct;


MPIEngine::MPIEngine ( const string& filename) : GreasyEngine(filename){
  
  engineType="mpi";
  workerId = -1;
  nworkers = 0;
  
}

bool MPIEngine::isMaster() { 
  
  return (workerId==0); 
  
}

bool MPIEngine::isWorker() { 
  
  return (workerId>0); 
  
}

void MPIEngine::init() {
  
  //Dummy arguments for MPI::Init
  int argc;
  char **argv;
  
  log->record(GreasyLog::devel, "MPIEngine::init", "Entering...");
  
  MPI::Init(argc, argv);
  workerId = MPI::COMM_WORLD.Get_rank();
  nworkers = MPI::COMM_WORLD.Get_size();
  
  // We don't count the master
  nworkers--;
  
  // Only the master has to perform the initialization of tasks.
  if (isMaster()) { 
    baseInit();
    // Fill the freeRank queue
    for (int i=1;i<=nworkers; i++) {
      freeWorkers.push(i);
    }
  }
  if (isWorker()) {
    // Disable signal handling for workers.
    // We only want to have the master in charge of the restarts and messages.
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    
    //Get the name of the node of this worker
    int size = MPI_MAX_PROCESSOR_NAME;
    MPI_Get_processor_name(hostname,&size);
    
    //Worker at this point is ready.
    ready = true;
    
  }
  
  log->record(GreasyLog::devel, "MPIEngine::init", "Exiting...");
  
}

void MPIEngine::finalize() {
  
  MPI::Finalize();
  
  // The master has to do some cleanup.
  if (isMaster()) baseFinalize();
  
}

void MPIEngine::run() {
  
  log->record(GreasyLog::devel, "MPIEngine::run", "Entering...");
  
  if (isReady()) {
    if (isMaster()) runMaster();
    else if (isWorker()) runWorker();
    else log->record(GreasyLog::error,  "Could not run MPI engine");
  } else {
    if (isMaster()) {
      log->record(GreasyLog::error, "Engine is not ready to run. Check your task file and configuration.");
      fireWorkers();
    }
  }

  log->record(GreasyLog::devel, "MPIEngine::run", "Exiting...");

}

/*
 * Master Methods
 * 
 */

void MPIEngine::runMaster() {
  
  //Master code
  set<int>::iterator it;
  GreasyTask* task = NULL;
  
  if (!isMaster()) return;
  log->record(GreasyLog::devel, "MPIEngine::runMaster", "Entering...");
  
  // Dummy check: let's see if there is any worker...
  if (nworkers==0) {
    log->record(GreasyLog::error, "No workers found. Rerun greasy with more resources");    
    return;
  }
  
  globalTimer.start();

  // Initialize the task queue with all the tasks ready to be executed
  for (it=validTasks.begin();it!=validTasks.end(); it++) {
    task = taskMap[*it];
    if (task->isWaiting()) taskQueue.push(task);
    else if (task->isBlocked()) blockedTasks.insert(task);
  }
   
  // Main Scheduling loop
  while (!(taskQueue.empty())||!(blockedTasks.empty())) {
    while (!taskQueue.empty()) {
      if (!freeWorkers.empty()) {
	// There is room to allocate a task...
	task =  taskQueue.front();
	taskQueue.pop();
	allocate(task);
      } else {
	// All workers are busy. We need to wait anyone to finish.
	waitForAnyWorker();
      }
    }
    
    if (!(blockedTasks.empty())) {
      // There are no tasks to be scheduled on the queue, but there are
      // dependencies not fulfilled and tasks already running, so we have
      // to wait for them to finish to release blocks on them.
      waitForAnyWorker();
    }
  }

  // At this point, all tasks are allocated / finished
  // Wait for the last tasks to complete
  while (freeWorkers.size()!=nworkers) {
    waitForAnyWorker();
  }
  
  // At this point all tasks have finished and all nodes are free
  // Let's fire the workers!
  fireWorkers();
  
  globalTimer.stop();
  
  log->record(GreasyLog::devel, "MPIEngine::runMaster", "Exiting...");
  
}

void MPIEngine::writeRestartFile() {
 
  if (isMaster()) baseWriteRestartFile();
  
}

void MPIEngine::allocate(GreasyTask* task) {
  
  int worker, cmdSize;
  const char *cmd;
  
  log->record(GreasyLog::devel, "MPIEngine::allocate", "Entering...");
   
  worker = freeWorkers.front();
  freeWorkers.pop();
  
  log->record(GreasyLog::info,  "Allocating task " + toString(task->getTaskId()));
  
  log->record(GreasyLog::debug, "MPIEngine::allocate", "Sending task " + toString(task->getTaskId()) + " to worker " + toString(worker));
  taskAssignation[worker] = task->getTaskId();
  task->setTaskState(GreasyTask::running);
  
  // Send the command size and the actual command
  cmdSize = task->getCommand().size()+1;
  cmd = task->getCommand().c_str();
  MPI::COMM_WORLD.Send( &cmdSize, 1, MPI::INT, worker, 0);
  MPI::COMM_WORLD.Send((void*)cmd, cmdSize, MPI::CHAR, worker , 0);
  
  log->record(GreasyLog::devel, "MPIEngine::allocate", "Exiting...");
  
}

void MPIEngine::waitForAnyWorker() {
  
  int retcode;
  int worker;
  int maxRetries=0;
  GreasyTask* task = NULL;
  MPI::Status status;
  reportStruct report; 
  
  log->record(GreasyLog::devel, "MPIEngine::waitForAnyWorker", "Entering...");
  
  if (config->keyExists("MaxRetries")) fromString(maxRetries, config->getValue("MaxRetries"));
  
  log->record(GreasyLog::debug,  "Waiting for any task to complete...");
  MPI::COMM_WORLD.Recv ( &report, sizeof(reportStruct), MPI::CHAR, MPI::ANY_SOURCE, MPI::ANY_TAG, status );
  retcode = report.retcode;
  
  worker = status.Get_source ();
  task = taskMap[taskAssignation[worker]];
  
  // Update task info with the report
  task->setElapsedTime(report.elapsed);
  task->setReturnCode(report.retcode);
  task->setHostname(string(report.hostname));
  
  // Push worker to the free workers queue again
  freeWorkers.push(worker);
  
  if (report.retcode != 0) {
    log->record(GreasyLog::error,  "Task " + toString(task->getTaskId()) + 
		    " failed with exit code " + toString(report.retcode) + " on node " + 
		    task->getHostname() +". Elapsed: " + GreasyTimer::secsToTime(report.elapsed));
    // Task failed, let's retry if we need to
    if ((maxRetries > 0) && (task->getRetries() < maxRetries)) {
      log->record(GreasyLog::warning,  "Retry "+ toString(task->getRetries()) + 
		    "/" + toString(maxRetries) + " of task " + toString(task->getTaskId()));
      task->addRetryAttempt();
      allocate(task);
    } else {
      task->setTaskState(GreasyTask::failed);
      updateDependencies(task);
    }
  } else {
    log->record(GreasyLog::info,  "Task " + toString(task->getTaskId()) + 
		    " completed successfully on node " + task->getHostname() + ". Elapsed: " + 
		    GreasyTimer::secsToTime(report.elapsed));
    task->setTaskState(GreasyTask::completed);
    updateDependencies(task);
  }
  
  log->record(GreasyLog::devel, "MPIEngine::waitForAnyWorker", "Exiting...");
  
}

void MPIEngine::updateDependencies(GreasyTask* parent) {
  
  int taskId, state;
  GreasyTask* child;
  list<int>::iterator it;

  log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Entering...");
  
  taskId = parent->getTaskId();
  state = parent->getTaskState();
  log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Inspecting reverse deps for task " + toString(taskId));
  
  if ( revDepMap.find(taskId) == revDepMap.end() ){
      log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "The task "+ toString(taskId) + " does not have any other dependendant task. No update done.");
      log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Exiting...");
      return;
  }

  for(it=revDepMap[taskId].begin() ; it!=revDepMap[taskId].end();it++ ) {
    child = taskMap[*it];
    if (state == GreasyTask::completed) {
      log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Remove dependency " + toString(taskId) + " from task " + toString(child->getTaskId()));
      child->removeDependency(taskId);
      if (!child->hasDependencies()) { 
	log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Moving task from blocked set to the queue");
	blockedTasks.erase(child);
	taskQueue.push(child);
      } else {
	log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "The task still has dependencies, so leave it blocked");
      }
    }
    else if ((state == GreasyTask::failed)||(state == GreasyTask::cancelled)) {
      log->record(GreasyLog::warning,  "Cancelling task " + toString(child->getTaskId()) + " because of task " + toString(taskId) + " failure");
      log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Parent failed: cancelling task and removing it from blocked");
      child->setTaskState(GreasyTask::cancelled);
      blockedTasks.erase(child);
      updateDependencies(child);
    }
  }
  
  log->record(GreasyLog::devel, "MPIEngine::updateDependencies", "Exiting...");
  
}

void MPIEngine::fireWorkers() {
  
  int fired = -1;
  int worker;
  
  log->record(GreasyLog::devel, "MPIEngine::fireWorkers", "Entering...");
  
  log->record(GreasyLog::debug,  "Sending fire comand to all workers...");
  for(int worker=1;worker<=nworkers;worker++) {
    MPI::COMM_WORLD.Send( &fired, 1, MPI::INT, worker, 0);
  } 
  
  log->record(GreasyLog::devel, "MPIEngine::fireWorkers", "Exiting...");
  
}

void MPIEngine::dumpTasks() {

  if (isMaster()) {
    log->record(GreasyLog::devel, dumpTaskMap());
  }

}

/*
 * Worker Methods
 * 
 */

void MPIEngine::runWorker() {
  
  int cmdSize = 0;
  char *cmd = NULL;
  MPI::Status status;
  int retcode = -1;
  int err;
  reportStruct report;
  GreasyTimer timer;
  
  // Worker code  
  if (!isWorker()) return;
  
  log->record(GreasyLog::devel, "MPIEngine::runWorker("+toString(workerId)+")", "Entering...");
  
  // Main worker loop 
  while(1) {
    report.workerStatus = 1;
    report.retcode = -1;
    report.elapsed = 0;
    strcpy(report.hostname,hostname);
    // Receive command size, including '\0'
    try {
      MPI::COMM_WORLD.Recv ( &cmdSize, 1, MPI::INT, 0, 0, status );
    } catch (MPI::Exception e) {
      log->record(GreasyLog::error, "WORKER("+toString(workerId)+")", "Error receiving command size: "+toString(e.Get_error_string()));
      report.workerStatus = 0;
      MPI::COMM_WORLD.Send( &report, sizeof(report), MPI::CHAR, 0, 0);
      continue;
    }
    
    // A negative size means the worker is fired
    if (cmdSize < 0) {
      log->record(GreasyLog::debug, toString(workerId), "Received fired signal!");  
      break;
    }
    
    // Allocate the memory for the command
    cmd = (char*) malloc(cmdSize*sizeof(char));
    if(!cmd) {
      log->record(GreasyLog::error, "WORKER("+toString(workerId)+")", "Could not allocate memory");
      report.workerStatus = 0;
      MPI::COMM_WORLD.Send( &report, sizeof(report), MPI::CHAR, 0, 0);
      break;
    }
    
    // Receive the command
    try {
      MPI::COMM_WORLD.Recv ( cmd, cmdSize, MPI::CHAR, 0, 0, status );
    } catch (MPI::Exception e) {
      log->record(GreasyLog::error, "WORKER("+toString(workerId)+")", "Error receiving command: "+toString(e.Get_error_string()));
      report.workerStatus = 0;
      MPI::COMM_WORLD.Send( &report, sizeof(report), MPI::CHAR, 0, 0);
      continue;
    }

    log->record(GreasyLog::debug, toString(workerId), "Running task " + toString(cmd));

    // Execute the command
    timer.reset();
    timer.start();
    retcode = system(cmd);    
    timer.stop();
    
    report.retcode = retcode;
    report.elapsed = timer.secsElapsed();
    
    // Report to the master the end of the task
    log->record(GreasyLog::debug, toString(workerId), "Task finished with retcode (" + toString(retcode) + "). Elapsed: " + GreasyTimer::secsToTime(report.elapsed));
    MPI::COMM_WORLD.Send( &report, sizeof(report), MPI::CHAR, 0, 0);
    
    if (cmd) free(cmd);
  }
  
    
  log->record(GreasyLog::devel, "MPIEngine::runWorker("+toString(workerId)+")", "Exiting...");
  
}
