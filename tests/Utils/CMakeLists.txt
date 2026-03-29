# Legacy test for internal utility class
add_executable(test_timingstats test_timingstats.cpp)
target_link_libraries(test_timingstats PRIVATE ebus)

add_executable(test_common test_common.cpp)
target_link_libraries(test_common PRIVATE ebus)