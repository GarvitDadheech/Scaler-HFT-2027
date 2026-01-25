#include "producer.h"

#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>

Producer::Producer(const std::string &shm_name, size_t total)
    : SHM_name(shm_name), totalBytes(total) {
  // 1. WE NEED  A MEMORY OF 16 MB FROM KERNEL
  const char *shm_file = "/kartik_shm";

  // ::open("sdfhdsjds")
  int shm_fd = ::shm_open(shm_file, O_CREAT | O_RDWR, 0777);
  if (shm_fd ==  -1)
    std::cout <<"producer didn't get the file\n";
  ::ftruncate(shm_fd,
              totalBytes + 4 + 4); // allocating 4,4 bytes for push and pop

  void *raw_ptr = ::mmap(nullptr, totalBytes + 4 + 4, PROT_READ | PROT_WRITE,
                         MAP_SHARED, shm_fd, 0);
  mSHMPtr = reinterpret_cast<SHM *>(raw_ptr);
}

void Producer::run() {
  srand(time(NULL));
  while (true) {
    size_t store = rand();
    store = store % 1000;
    std::cout << store << "\n";
    // mSHMPtr->push(store)
  }
}


/*
*  // shm_ptr raw ptr to 8 bytes
// Vivek* ptr= reinterpret_cast<Vivek*>(shm_ptr);
// ptr->marks = 0;
// ptr->wickets = 10;
// std::cout << ptr->marks << std::endl;
// char* ptr = reinterpret_cast<char*>(shm_ptr);
//
// for (int i=0;i<100;i++) {
//   ptr[i] = 'a';
// }
// std::cout << ptr[10] << std::endl;
}/
*/
