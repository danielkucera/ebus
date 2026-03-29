add_executable(test_device test_device.cpp)
target_link_libraries(test_device PRIVATE ebus)

add_executable(test_datatypes test_datatypes.cpp)
target_link_libraries(test_datatypes PRIVATE ebus)