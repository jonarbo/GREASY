#include "slurmengine.h"

SlurmEngine::SlurmEngine (const string& filename ): GreasyEngine(filename) {
  
  engineType="slurm";
  
}

void SlurmEngine::run() {
  
  map<int,GreasyTask*>::iterator it;
  log->record(GreasyLog::devel, "SlurmEngine::run", "Entering...");
  
  log->record(GreasyLog::devel, "SlurmEngine::run", "Let's see what we've got...");
  for (it=taskMap.begin();it!=taskMap.end(); it++) {
    log->record(GreasyLog::devel, it->second->dump());
  }
  
  log->record(GreasyLog::devel, "SlurmEngine::run", "Exiting...");
  
}
