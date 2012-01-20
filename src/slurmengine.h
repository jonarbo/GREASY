#ifndef SLURMENGINE_H
#define SLURMENGINE_H

#include <string>

#include "abstractengine.h"

/**
  * class SlurmEngine
  * 
  */
class SlurmEngine : public AbstractEngine
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
