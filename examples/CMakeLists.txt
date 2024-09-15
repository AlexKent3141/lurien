
add_executable(basic_example example.cpp)

target_link_libraries(basic_example
  PRIVATE
    lurien)

set_target_properties(basic_example
  PROPERTIES
    CXX_STANDARD 20)
