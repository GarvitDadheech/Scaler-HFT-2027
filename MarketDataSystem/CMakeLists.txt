cmake_minimum_required(VERSION 3.20)
project(HFT_MarketData CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance optimization flags
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find required packages
find_package(fmt REQUIRED)

# Include directory
include_directories(${PROJECT_SOURCE_DIR})

# Platform-specific libraries
if(APPLE)
    set(PLATFORM_LIBS pthread)
elseif(UNIX)
    set(PLATFORM_LIBS pthread rt)
else()
    set(PLATFORM_LIBS pthread)
endif()

# Publisher executable
add_executable(publisher src/publisher.cpp)
target_link_libraries(publisher PRIVATE fmt::fmt ${PLATFORM_LIBS})
target_compile_options(publisher PRIVATE -Wall -Wextra)

# Consumer SHM executable
add_executable(consumer_shm src/consumer_shm.cpp)
target_link_libraries(consumer_shm PRIVATE fmt::fmt ${PLATFORM_LIBS})
target_compile_options(consumer_shm PRIVATE -Wall -Wextra)

# Consumer TCP executable
add_executable(consumer_tcp src/consumer_tcp.cpp)
target_link_libraries(consumer_tcp PRIVATE fmt::fmt ${PLATFORM_LIBS})
target_compile_options(consumer_tcp PRIVATE -Wall -Wextra)

# Print build information
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Compiler flags: ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE}")
