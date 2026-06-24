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

# Live web-app server (browser trading terminal). cpp-httplib is header-only.
find_package(Threads REQUIRED)
add_executable(exchange server/server.cpp server/MarketSimulator.cpp)
target_include_directories(exchange PRIVATE server)
target_link_libraries(exchange PRIVATE lob_core Threads::Threads)
if(WIN32)
    target_link_libraries(exchange PRIVATE ws2_32 wsock32)
endif()

# Unit tests (run with: ctest  or  ./lob_tests).
enable_testing()
add_executable(lob_tests tests/test_orderbook.cpp)
target_link_libraries(lob_tests PRIVATE lob_core)
add_test(NAME lob_tests COMMAND lob_tests)
