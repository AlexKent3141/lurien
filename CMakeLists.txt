cmake_minimum_required(VERSION 3.16)
project(lurien_profiler)

add_library(lurien
  INTERFACE)

target_include_directories(lurien
  INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)

target_link_libraries(lurien
  INTERFACE
    Threads::Threads)
