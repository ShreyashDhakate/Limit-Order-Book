#!/usr/bin/env bash
# Zero-dependency build script: just needs g++ (C++17). Works without make/cmake.
#
#   ./build.sh         -> build the driver, then run it on data/orders.txt
#   ./build.sh test    -> build and run the unit-test suite
set -euo pipefail

CXX="${CXX:-g++}"
FLAGS="-std=c++17 -O2 -Wall -Wextra -Iinclude"

if [[ "${1:-run}" == "test" ]]; then
    $CXX $FLAGS tests/test_orderbook.cpp src/OrderBook.cpp -o lob_tests
    ./lob_tests
else
    $CXX $FLAGS src/main.cpp src/OrderBook.cpp -o lob
    ./lob data/orders.txt
fi
