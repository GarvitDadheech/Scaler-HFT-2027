cmake_minimum_required(VERSION 3.5)
project(boost_tcp_server)


set(CMAKE_CXX_STANDARD 17)

find_package(Boost REQUIRED COMPONENTS system)
add_executable(boost_tcp_server main.cpp)


target_link_libraries(boost_tcp_server PRIVATE Boost::system)