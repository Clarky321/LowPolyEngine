@echo off
rem Shared helpers for the scripts in this folder. Not meant to be run directly.
rem
rem Usage from another script (which must use setlocal):
rem   call "%~dp0_common.bat" :helper [args]
rem   if %errorlevel% neq 0 exit /b %errorlevel%
rem
rem Helpers set LPE_* variables in the caller's environment:
rem   always          LPE_ROOT
rem   :resolve_config LPE_CONFIG, LPE_PRESET, LPE_BUILD_DIR
rem   :find_cmake     LPE_CMAKE, LPE_CTEST
rem   :find_conan     LPE_CONAN
rem   :find_clang_format LPE_CLANG_FORMAT
rem   :find_git       LPE_GIT

rem Resolve the root before "shift" below, which also shifts %0.
for %%I in ("%~dp0..\..") do set "LPE_ROOT=%%~fI"
if not exist "%LPE_ROOT%\CMakeLists.txt" goto :bad_root
if "%~1"=="" goto :no_helper

set "LPE_HELPER=%~1"
shift
goto %LPE_HELPER%

:bad_root
echo error: repository root not found at "%LPE_ROOT%" 1>&2
exit /b 1

:no_helper
echo error: _common.bat needs a helper name 1>&2
exit /b 1


rem ---------------------------------------------------------------------------
rem :resolve_config [debug|release]  - default is debug.
rem Preset names must match CMakePresets.json.
:resolve_config
if "%~1"=="" goto :config_debug
if /i "%~1"=="debug" goto :config_debug
if /i "%~1"=="release" goto :config_release
echo error: unknown configuration "%~1", expected debug or release 1>&2
exit /b 2

:config_debug
set "LPE_CONFIG=Debug"
set "LPE_PRESET=windows-msvc-debug"
goto :config_done

:config_release
set "LPE_CONFIG=Release"
set "LPE_PRESET=windows-msvc-release"

:config_done
set "LPE_BUILD_DIR=%LPE_ROOT%\build\%LPE_PRESET%"
exit /b 0


rem ---------------------------------------------------------------------------
:find_cmake
call :find_tool LPE_CMAKE cmake.exe "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if %errorlevel% neq 0 exit /b %errorlevel%
rem Take ctest from the same folder so both come from one CMake version.
for %%I in ("%LPE_CMAKE%") do set "LPE_CTEST=%%~dpIctest.exe"
if not exist "%LPE_CTEST%" goto :ctest_missing
exit /b 0

:ctest_missing
echo error: ctest.exe not found next to "%LPE_CMAKE%" 1>&2
exit /b 1


rem ---------------------------------------------------------------------------
:find_conan
rem Prefer the project-local virtual environment described in README.md.
set "LPE_CONAN=%LPE_ROOT%\.venv\Scripts\conan.exe"
if exist "%LPE_CONAN%" exit /b 0
call :find_tool LPE_CONAN conan.exe ""
if %errorlevel% neq 0 goto :conan_missing
exit /b 0

:conan_missing
echo Install it locally: python -m venv .venv ^&^& .venv\Scripts\python -m pip install conan 1>&2
exit /b 1


rem ---------------------------------------------------------------------------
:find_clang_format
call :find_tool LPE_CLANG_FORMAT clang-format.exe "VC\Tools\Llvm\x64\bin\clang-format.exe"
exit /b %errorlevel%


rem ---------------------------------------------------------------------------
:find_git
call :find_tool LPE_GIT git.exe ""
exit /b %errorlevel%


rem ---------------------------------------------------------------------------
rem :find_tool <variable> <exe name> <path inside a Visual Studio installation or "">
rem Looks on PATH first, then in the newest Visual Studio installation.
:find_tool
set "%~1="
for /f "delims=" %%P in ('where %~2 2^>nul') do if not defined %~1 set "%~1=%%P"
if defined %~1 goto :tool_found
if "%~3"=="" goto :tool_missing
if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" goto :tool_missing
rem Run vswhere from its own folder: a quoted "(x86)" path inside the for /f
rem command line is misparsed by cmd. ".\" is required because
rem NoDefaultCurrentDirectoryInExePath may disable the current-folder lookup.
pushd "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
for /f "delims=" %%P in ('.\vswhere.exe -latest -products * -find "%~3"') do if not defined %~1 set "%~1=%%P"
popd
if defined %~1 goto :tool_found

:tool_missing
echo error: %~2 not found on PATH or in a Visual Studio installation 1>&2
exit /b 1

:tool_found
call echo using %~2: "%%%~1%%"
exit /b 0
