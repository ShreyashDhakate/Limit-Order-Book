@echo off
REM Build and launch the C++ exchange web server on Windows.
REM Double-click this file, or run it from the LOB project root.
cd /d "%~dp0\.."
echo Building server...
g++ -std=c++17 -O2 -Iinclude -Iserver server\server.cpp server\MarketSimulator.cpp src\OrderBook.cpp -o server\exchange.exe -lws2_32 -lwsock32 -pthread
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)
echo.
echo Server starting -- open http://localhost:8080 in your browser.
echo Press Ctrl+C to stop.
echo.
server\exchange.exe 8080
pause
