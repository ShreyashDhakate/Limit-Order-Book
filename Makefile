# Simple Makefile for systems with GNU make. (No make? use ./build.sh instead.)
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
ENGINE    = src/OrderBook.cpp

.PHONY: all run test clean

all: lob

# Build the command-line driver.
lob: src/main.cpp $(ENGINE) include/Order.h include/OrderBook.h
	$(CXX) $(CXXFLAGS) src/main.cpp $(ENGINE) -o lob

# Build + run the driver against the sample order flow.
run: lob
	./lob data/orders.txt

# Build + run the unit-test suite.
test: tests/test_orderbook.cpp $(ENGINE) include/Order.h include/OrderBook.h
	$(CXX) $(CXXFLAGS) tests/test_orderbook.cpp $(ENGINE) -o lob_tests
	./lob_tests

clean:
	rm -f lob lob_tests trades.csv *.o
