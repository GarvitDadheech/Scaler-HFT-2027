#pragma once
#include "SHM.h"

#include <string>
class Producer {
private:
  std::string SHM_name;
  size_t totalBytes;
  SHM* mSHMPtr;

public:
  Producer(const std::string &, size_t);

  void run();
};