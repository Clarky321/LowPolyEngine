@echo off
rem Install Conan dependencies and configure the CMake preset.
rem Usage: configure.bat [debug|release]   (default: debug)
setlocal

call "%~dp0_common.bat" :resolve_config %1
if %errorlevel% neq 0 exit /b %errorlevel%
call "%~dp0_common.bat" :find_conan
if %errorlevel% neq 0 exit /b %errorlevel%
call "%~dp0_common.bat" :find_cmake
if %errorlevel% neq 0 exit /b %errorlevel%

pushd "%LPE_ROOT%"

rem Same commands as README.md; the preset reads its toolchain from build\<preset>\conan.
rem conan.lock pins every version and recipe revision (ADR 0024).
"%LPE_CONAN%" install . -pr:h=profiles/windows-msvc -pr:b=profiles/windows-msvc -s build_type=%LPE_CONFIG% --lockfile=conan.lock --build=missing -of "build/%LPE_PRESET%/conan"
if %errorlevel% neq 0 goto :failed

"%LPE_CMAKE%" --preset %LPE_PRESET%
if %errorlevel% neq 0 goto :failed

popd
exit /b 0

:failed
set "LPE_EXIT=%errorlevel%"
popd
exit /b %LPE_EXIT%
