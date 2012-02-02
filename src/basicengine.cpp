#include "basicengine.h"
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

BasicEngine::BasicEngine ( const string& filename) : AbstractSchedulerEngine(filename){
  
  engineType="basic";
  remote = false;
}

void BasicEngine::init() {
    
  log->record(GreasyLog::devel, "BasicEngine::init", "Entering...");

  AbstractSchedulerEngine::init();
  
  char hostname[HOST_NAME_MAX];

  gethostname(hostname, sizeof(hostname));
  masterHostname=hostname;

  // Setup the nodelist
  if (config->keyExists("NodeList")) {
    vector<string> nodelist = split(config->getValue("NodeList"),',');
    if (nworkers>nodelist.size()){
      log->record(GreasyLog::error,  "Requested more workers than nodes in the nodelist. Check parameters Nworkers and NodeList");
      ready = false;
    } else {
      // Fill the workerNodes map
      for (int i=1;i<=nworkers; i++) {
	workerNodes[i]=nodelist[i-1];
	if (!isLocalNode(workerNodes[i])) remote = true;
      }
    }
    
    if (remote) {
#if BASIC_SPAWN == srun
      log->record(GreasyLog::debug,  "Using srun to spawn remote tasks");
#elif BASIC_SPAWN == srun
      log->record(GreasyLog::debug,  "Using ssh to spawn remote tasks");
#else
      log->record(GreasyLog::error,  "No Basic Spawner in the system. Please run greasy locally unsetting the NodeList Parameter");
#endif    
      
    }

  }

   
  log->record(GreasyLog::devel, "BasicEngine::init", "Exiting...");
  
}

void BasicEngine::run() {
  
  log->record(GreasyLog::devel, "BasicEngine::run", "Entering...");
  
  if (isReady()) runScheduler();

  log->record(GreasyLog::devel, "BasicEngine::run", "Exiting...");

}

void BasicEngine::allocate(GreasyTask* task) {
  
  int worker;
  
  log->record(GreasyLog::devel, "BasicEngine::allocate", "Entering...");
  
  log->record(GreasyLog::info,  "Allocating task " + toString(task->getTaskId()));
  
  worker = freeWorkers.front();
  freeWorkers.pop();
  taskAssignation[worker] = task->getTaskId();
  
  pid_t pid = fork();
  
  if (pid == 0) {
   //Child process will exec command...
   // We use system instead of exec because of greater compatibility with command to be executed
   exit(executeTask(task,worker));  
  } else if (pid > 0) {
    // parent
    log->record(GreasyLog::debug,  "BasicEngine::allocate", "Task " 
		      + toString(task->getTaskId()) + " to worker " + toString(worker)
		      + " on node " + getWorkerNode(worker));
    pidToWorker[pid] = worker;
    task->setTaskState(GreasyTask::running);
    workerTimers[worker].reset();
    workerTimers[worker].start();
    
  } else {
   //error 
   log->record(GreasyLog::error,  "Could not execute a new process");
   task->setTaskState(GreasyTask::failed);
   task->setReturnCode(-1);
   freeWorkers.push(worker);
  }
  
  log->record(GreasyLog::devel, "BasicEngine::allocate", "Exiting...");
  
}

void BasicEngine::waitForAnyWorker() {
  
  int retcode = -1;
  int worker;
  pid_t pid;
  int status;
  int maxRetries=0;
  GreasyTask* task = NULL;
  
  log->record(GreasyLog::devel, "BasicEngine::waitForAnyWorker", "Entering...");
  
  if (config->keyExists("MaxRetries")) fromString(maxRetries, config->getValue("MaxRetries"));
  
  log->record(GreasyLog::debug,  "Waiting for any task to complete...");
  pid = wait(&status);

  // Get the return code
  if (WIFEXITED(status)) retcode = WEXITSTATUS(status);

  worker = pidToWorker[pid];
    
  // Push worker to the free workers queue again
  freeWorkers.push(worker);
  workerTimers[worker].stop();
  
  // Update task info
  task = taskMap[taskAssignation[worker]];
  task->setReturnCode(retcode);
  task->setElapsedTime(workerTimers[worker].secsElapsed());
  task->setHostname(getWorkerNode(worker));
  task->setHostname(masterHostname);

  taskEpilogue(task);
  
  log->record(GreasyLog::devel, "BasicEngine::waitForAnyWorker", "Exiting...");
  
}

int BasicEngine::executeTask(GreasyTask *task, int worker) {
  
  log->record(GreasyLog::devel, "BasicEngine::executeTask["+toString(worker) +"]", "Entering..."); 
  string command = "";
  string node = "";

  if (!remote) {
    node = masterHostname;
  } else {
    node = workerNodes[worker];
    if (!isLocalNode(node)) {
      #if BASIC_SPAWN == srun
	  command = "srun -n 1 -w " + node + " ";
      #elif BASIC_SPAWN == ssh
	  command = "ssh " + node + " " ;
      #endif
    }
  }
  
  command += task->getCommand();
  log->record(GreasyLog::devel,  "BasicEngine::executeTask[" + toString(worker) +"]", "Task " 
		      + toString(task->getTaskId()) + " on node " + node + " command: " + command);
  int ret =  system(command.c_str());
  log->record(GreasyLog::devel, "BasicEngine::executeTask["+toString(worker) +"]", "Exiting..."); 
  return ret;
  
}

bool BasicEngine::isLocalNode(string node) {
  
  return ((node == masterHostname)||(node == "localhost"));
  
}

string BasicEngine::getWorkerNode(int worker) {
  
  if (remote) return workerNodes[worker];
  else return masterHostname;
  
}
