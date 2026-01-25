#include <fcntl.h>
#include <iostream>
#include <unistd.h>
int main(int argv, char **argc) {
  auto appType = std::string(argc[1]);
  std::cout << appType << std::endl;
  if (appType == "writer") {
    int fileFd = ::open("shared_file.txt", O_RDWR | O_CREAT, 0777);
    std::cout << fileFd << std::endl;
    while (true) {
      ::write(fileFd, "hello", 5);
      ::sleep(1);
    }
  } else if (appType == "reader") {
    int fileFd = ::open("shared_file.txt", O_RDWR | O_CREAT);
    while (true) {
      char buffer[1024];
      int bytes = ::read(fileFd, buffer, 1024);
      if (bytes <= 0)
        continue;
      for (size_t i = 0; i < bytes; i++)
        std::cout << buffer[i];
      std::cout << std::endl;
    }
  }
  return EXIT_SUCCESS;
}