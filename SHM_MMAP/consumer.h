#pragma once
#include "SHM.h"

#include <string>
class Consumer {
private:
  std::string SHM_name;
  size_t totalBytes;
  SHM* mSHMPtr;

public:
  Consumer(const std::string&, size_t );

  void run();
};