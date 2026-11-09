@echo off
rem Run the CTest suite of an already built configuration.
rem Usage: test.bat [debug|release]   (default: debug)
setlocal

call "%~dp0_common.bat" :resolve_config %1
if %errorlevel% neq 0 exit /b %errorlevel%
call "%~dp0_common.bat" :find_cmake
if %errorlevel% neq 0 exit /b %errorlevel%

if not exist "%LPE_BUILD_DIR%\CMakeCache.txt" goto :not_configured

pushd "%LPE_ROOT%"
"%LPE_CTEST%" --preset %LPE_PRESET%
set "LPE_EXIT=%errorlevel%"
popd
exit /b %LPE_EXIT%

:not_configured
echo error: %LPE_PRESET% is not configured. Run: scripts\windows\build.bat %LPE_CONFIG% 1>&2
exit /b 1
