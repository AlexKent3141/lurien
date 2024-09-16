
add_executable(basic_example basic_example.cpp)

target_link_libraries(basic_example
  PRIVATE
    lurien)

set_target_properties(basic_example
  PROPERTIES
    CXX_STANDARD 20)

add_executable(recursive_example recursive_example.cpp)

target_link_libraries(recursive_example
  PRIVATE
    lurien)

set_target_properties(recursive_example
  PROPERTIES
    CXX_STANDARD 20)
