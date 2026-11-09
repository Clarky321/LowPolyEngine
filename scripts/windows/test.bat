@echo off
rem Run the CTest suite of an already built configuration.
rem Usage: test.bat [debug|release] [gpu]   (default: debug, headless tests)
rem "gpu" runs only the rendering tier (needs a desktop session and OpenGL 4.5).
setlocal

call "%~dp0_common.bat" :resolve_config %1
if %errorlevel% neq 0 exit /b %errorlevel%
call "%~dp0_common.bat" :find_cmake
if %errorlevel% neq 0 exit /b %errorlevel%

set "LPE_TEST_PRESET=%LPE_PRESET%"
if "%~2"=="" goto :tier_selected
if /i "%~2"=="gpu" goto :gpu_tier
echo error: unknown test tier "%~2", expected gpu 1>&2
exit /b 2

:gpu_tier
set "LPE_TEST_PRESET=%LPE_PRESET%-gpu"

:tier_selected
if not exist "%LPE_BUILD_DIR%\CMakeCache.txt" goto :not_configured

pushd "%LPE_ROOT%"
"%LPE_CTEST%" --preset %LPE_TEST_PRESET%
set "LPE_EXIT=%errorlevel%"
popd
exit /b %LPE_EXIT%

:not_configured
echo error: %LPE_PRESET% is not configured. Run: scripts\windows\build.bat %LPE_CONFIG% 1>&2
exit /b 1
