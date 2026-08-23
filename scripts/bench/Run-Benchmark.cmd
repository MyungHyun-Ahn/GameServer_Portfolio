@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0Run-Benchmark.ps1" %*
endlocal
