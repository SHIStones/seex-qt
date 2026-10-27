@echo off
setlocal

rem vcvars64.bat asks reg.exe where the Windows SDK lives; the sandbox blocks
rem that, so the SDK include/lib/bin paths are added here by hand instead.
set SDKROOT=C:\Program Files (x86)\Windows Kits\10
set SDKVER=10.0.26100.0

set INCLUDE=%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\winrt;%SDKROOT%\Include\%SDKVER%\cppwinrt
set LIB=%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64
set PATH=%SDKROOT%\bin\%SDKVER%\x64;D:\Python\Python312\Scripts;%PATH%

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

rem vcvars appends its own SDK guesses; put the explicit ones back in front.
set INCLUDE=%SDKROOT%\Include\%SDKVER%\ucrt;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\winrt;%SDKROOT%\Include\%SDKVER%\cppwinrt;%INCLUDE%
set LIB=%SDKROOT%\Lib\%SDKVER%\ucrt\x64;%SDKROOT%\Lib\%SDKVER%\um\x64;%LIB%

set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "F:\seex\seex-qt\build\CMakeCache.txt" (
  %CMAKE% -S F:\seex\seex-qt -B F:\seex\seex-qt\build -G Ninja ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
  if errorlevel 1 exit /b 1
)

%CMAKE% --build F:\seex\seex-qt\build --parallel
