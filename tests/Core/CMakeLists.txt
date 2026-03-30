# Catch2 Tests (Modern)
add_executable(catch_sequence catch_sequence.cpp)
target_link_libraries(catch_sequence PRIVATE ebus Catch2::Catch2WithMain)

add_executable(catch_telegram catch_telegram.cpp)
target_link_libraries(catch_telegram PRIVATE ebus Catch2::Catch2WithMain)

add_executable(catch_request catch_request.cpp)
target_link_libraries(catch_request PRIVATE ebus Catch2::Catch2WithMain)

add_executable(catch_handler catch_handler.cpp)
target_link_libraries(catch_handler PRIVATE ebus Catch2::Catch2WithMain)

# Legacy Tests (Manual main)
add_executable(test_sequence test_sequence.cpp)
target_link_libraries(test_sequence PRIVATE ebus)

add_executable(test_telegram test_telegram.cpp)
target_link_libraries(test_telegram PRIVATE ebus)

add_executable(test_request test_request.cpp)
target_link_libraries(test_request PRIVATE ebus)

add_executable(test_handler test_handler.cpp)
target_link_libraries(test_handler PRIVATE ebus)

add_executable(test_bushandler test_bushandler.cpp)
target_link_libraries(test_bushandler PRIVATE ebus)
