#include "consumer.h"
#include <fcntl.h> // shm_open
#include <iostream>
#include <sys/mman.h>
#include <sys/mman.h> // mmap

Consumer::Consumer(const std::string &shm_name, size_t totalBytes)
    : SHM_name(shm_name) {
  const char *shm_file = "/kartik_shm";
  int shm_fd = ::shm_open(shm_file, O_RDWR, 0777);
  if (shm_fd == -1) {
    std::cout << "consumer didn't get the file\n";
  }
  void *ptrToRaw = ::mmap(nullptr, totalBytes + 4 + 4, PROT_READ | PROT_WRITE,
                          MAP_SHARED, shm_fd, 0);

  mSHMPtr = reinterpret_cast<SHM *>(ptrToRaw);
}

void Consumer::run() {
  std::cout << "Consumer running\n";
  int cnt = 0;
  while (cnt < 1000'000){
    int store = 0;
    bool status = mSHMPtr->pop(store);
    if (status) {
      cnt++;
    }
  }
  std::cout <<"done\n";
}