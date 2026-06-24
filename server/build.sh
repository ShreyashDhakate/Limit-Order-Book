#!/usr/bin/env bash
# Build the C++ exchange web server. Run from the LOB project root:
#     ./server/build.sh && ./server/exchange.exe 8080
# then open http://localhost:8080
set -euo pipefail

CXX="${CXX:-g++}"

# Winsock libs are only needed on Windows (MinGW/MSYS); Linux/macOS just need pthreads.
LIBS="-pthread"
case "$(uname -s 2>/dev/null)" in
  MINGW*|MSYS*|CYGWIN*) LIBS="-lws2_32 -lwsock32 -pthread" ;;
esac

$CXX -std=c++17 -O2 -Iinclude -Iserver \
     server/server.cpp server/MarketSimulator.cpp src/OrderBook.cpp \
     -o server/exchange.exe $LIBS

echo "Built server/exchange.exe"
echo "Run:  ./server/exchange.exe 8080   then open http://localhost:8080"
