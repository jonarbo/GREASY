#ifndef SLURMENGINE_H
#define SLURMENGINE_H

#include <string>

#include "greasyengine.h"

/**
  * class SlurmEngine
  * 
  */
class SlurmEngine : public GreasyEngine
{
  
public:

  /**
   * Empty Constructor
   */
  SlurmEngine ( const string& filename);
  
  void run();

protected:

};

#endif // SLURMENGINE_H
