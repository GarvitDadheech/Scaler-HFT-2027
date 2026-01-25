cmake_minimum_required(VERSION 3.5)
project(mmap_shm)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(benchmark REQUIRED)
add_executable(mmap_shm
        main.cpp
        producer.cpp
        consumer.cpp
        SHM.cpp
)
add_executable(bench_consumer
        benchmark_consumer.cpp
        producer.cpp
        consumer.cpp
        SHM.cpp
)

target_link_libraries(mmap_shm PRIVATE pthread)
target_link_libraries(bench_consumer
        PRIVATE benchmark::benchmark pthread
)
