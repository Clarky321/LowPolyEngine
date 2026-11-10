@echo off
rem Remove generated build directories of the Windows presets.
rem Only build\windows-msvc-debug and build\windows-msvc-release are deleted;
rem build\ itself is removed only if it is empty afterwards. Sources, assets,
rem docs, .git, .venv and the Conan package cache are never touched.
rem Usage: clean.bat [debug|release|all]   (default: all)
setlocal

set "LPE_EXIT=0"
if "%~1"=="" goto :clean_all
if /i "%~1"=="all" goto :clean_all

call "%~dp0_common.bat" :resolve_config %1
if %errorlevel% neq 0 goto :usage
call :remove_dir "%LPE_BUILD_DIR%"
goto :finish

:clean_all
call "%~dp0_common.bat" :resolve_config debug
if %errorlevel% neq 0 exit /b %errorlevel%
call :remove_dir "%LPE_BUILD_DIR%"
call "%~dp0_common.bat" :resolve_config release
if %errorlevel% neq 0 exit /b %errorlevel%
call :remove_dir "%LPE_BUILD_DIR%"

:finish
rem rd without /s fails when the folder is not empty, which is what we want.
rd "%LPE_ROOT%\build" 2>nul
exit /b %LPE_EXIT%

:usage
echo usage: clean.bat [debug^|release^|all] 1>&2
exit /b 2

:remove_dir
if not exist "%~1\" goto :nothing_to_remove
echo removing "%~1"
rmdir /s /q "%~1"
if exist "%~1\" goto :remove_failed
exit /b 0

:nothing_to_remove
echo nothing to remove: "%~1"
exit /b 0

:remove_failed
echo error: could not fully remove "%~1". Is a program still using files in it? 1>&2
set "LPE_EXIT=1"
exit /b 0
