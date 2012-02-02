#ifndef SLURMENGINE_H
#define SLURMENGINE_H

#include <string>
#include <queue>
#include <map>
#include <limits.h>

#include "basicengine.h"

/**
  * This engine inherits AbstractEngine, and implements a scheduler and launcher
  * for Greasy using an embeded Slurm
  */
class SlurmEngine : public AbstractEngine
{
  
public:

  /**
   * Constructor that adds the filename to process.
   * @param filename path to the task file.
   */
  SlurmEngine (const string& filename ); 
  

protected:
  
};


#endif // SLURMENGINE_H
