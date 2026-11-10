@echo off
rem Configure (Conan + CMake) and build all targets of a configuration.
rem Usage: build.bat [debug|release]   (default: debug)
setlocal

call "%~dp0_common.bat" :resolve_config %1
if %errorlevel% neq 0 exit /b %errorlevel%

rem Re-running configure is cheap when nothing changed and keeps the
rem dependency graph in sync with conanfile.py after edits.
call "%~dp0configure.bat" %1
if %errorlevel% neq 0 exit /b %errorlevel%

call "%~dp0_common.bat" :find_cmake
if %errorlevel% neq 0 exit /b %errorlevel%

pushd "%LPE_ROOT%"
"%LPE_CMAKE%" --build --preset %LPE_PRESET%
set "LPE_EXIT=%errorlevel%"
popd
exit /b %LPE_EXIT%
