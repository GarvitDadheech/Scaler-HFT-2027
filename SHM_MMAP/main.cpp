#include "consumer.h"
#include "producer.h"

#include <iostream>
#include <unistd.h>

using namespace std;
int main(int argc, char **argv) {

  auto app_type = argv[1];
  auto shm_name = argv[2];
  cout << app_type << endl;
  size_t totalBytesOfMemory = 16*1024*1024; // 16 mb
  if (std::string(app_type) == "producer") {
    Producer producer{std::string(shm_name), totalBytesOfMemory};
    std::cout <<"SLEEPING 2\n";
    ::sleep(2);
    producer.run();
  } else {
    Consumer consumer{std::string(shm_name), totalBytesOfMemory};
    consumer.run();
  }
}
