cmake_minimum_required(VERSION 3.14)
project(LimitOrderBook CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The matching engine, built once and shared by the app and the tests.
add_library(lob_core src/OrderBook.cpp)
target_include_directories(lob_core PUBLIC include)

# Command-line driver.
add_executable(lob src/main.cpp)
target_link_libraries(lob PRIVATE lob_core)

# Unit tests (run with: ctest  or  ./lob_tests).
enable_testing()
add_executable(lob_tests tests/test_orderbook.cpp)
target_link_libraries(lob_tests PRIVATE lob_core)
add_test(NAME lob_tests COMMAND lob_tests)
