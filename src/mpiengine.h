#ifndef MPIENGINE_H
#define MPIENGINE_H

#include <string>
#include <queue>
#include "mpi.h"

#include "greasyengine.h"

/**
  * This engine inherits GreasyEngine, and implements an MPI scheduler for Greasy.
  * 
  */
class MPIEngine : public GreasyEngine
{
  
public:

  /**
   * Constructor that adds the filename to process.
   * @param filename path to the task file.
   */
  MPIEngine (const string& filename );
  
  /**
   * Perform the initialization of the engine and the MPI environment.
   */
  void init();
  
  /**
   * Execute the engine. It is divided into 2 different parts, for master and workers.
   */
  void run();

  /**
   * Finalization of the engine and MPI environment.
   */
  void finalize();

  /**
   * Reimplementation of writeRestartFile() to prevent workers from 
   * writing the restart (only the master does the job).
   */
  void writeRestartFile();

  /**
   * Reimplementation of dumpTasks() to prevent workers from 
   * recording the dump (only the master does the job).
   */
  virtual void dumpTasks();

protected:
  
  /**
   * Check if the engine is in Master mode.
   * @return true if engine is the master, false otherwise.
   */
  bool isMaster();
  
  /**
   * Check if the engine is in Worker mode.
   * @return true if engine is a worker, false otherwise.
   */
  bool isWorker();

  // Master Methods
  /**
   * Main master method.
   * All the scheduling of tasks is done here.
   */
  void runMaster();
  
  /**
   * Allocate a task in a free worker, sending the command to it.
   * @param task A pointer to a GreasyTask object to allocate.
   */
  void allocate(GreasyTask* task);
  
  /**
   * Wait for any worker to complete their tasks and retrieve
   * results.
   */
  void waitForAnyWorker();
  
  /**
   * Send the end signal to the workers. This method should be called
   * when all tasks have been completed and we want to finalize workers.
   */
  void fireWorkers();
  
  /**
   * Update all the tasks depending from the parent task which has finished.
   * @param parent A pointer to a GreasyTask object that finished.
   */
  void updateDependencies(GreasyTask* parent);
  
  // Worker Methods
  /**
   * Main Worker method.
   * It waits for a message from the master with the command
   * to execute, until the end signal is received.
   */
  void runWorker();
  
  int workerId; /**<  Id of the worker. Master is 0. */
  map <int,int> taskAssignation; ///<  Map that holds the task assignation to workers.
				 ///< workerId -> taskId
  queue <int> freeWorkers; ///< The queue of free worker ids, from where the candidates
			   ///< to run a task will be taken.
  queue <GreasyTask*> taskQueue; ///< The queue of tasks to be executed.
  set <GreasyTask*> blockedTasks; ///< The set of blocked tasks.
  char hostname[MPI_MAX_PROCESSOR_NAME]; ///< Cstring to hold the worker hostname.

};

#endif // MPIENGINE_H
