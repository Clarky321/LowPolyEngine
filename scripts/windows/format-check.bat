@echo off
rem Check that C/C++ sources match .clang-format. Does not modify files.
rem Checks tracked and new untracked files; ignored files (build\, .venv\) are skipped.
rem Usage: format-check.bat
setlocal

call "%~dp0_common.bat" :find_clang_format
if %errorlevel% neq 0 exit /b %errorlevel%
call "%~dp0_common.bat" :find_git
if %errorlevel% neq 0 exit /b %errorlevel%

"%LPE_CLANG_FORMAT%" --version

set "LPE_FILES=0"
set "LPE_FAILED=0"
pushd "%LPE_ROOT%"
for /f "delims=" %%F in ('git ls-files --cached --others --exclude-standard -- "*.c" "*.cc" "*.cpp" "*.h" "*.hpp" "*.inl"') do call :check_file "%%F"
popd

echo format-check: %LPE_FILES% files checked, %LPE_FAILED% need formatting
if %LPE_FAILED% neq 0 exit /b 1
exit /b 0

:check_file
rem Skip files listed by git but deleted in the working tree.
if not exist "%~1" exit /b 0
set /a LPE_FILES+=1
"%LPE_CLANG_FORMAT%" --dry-run --Werror "%~1"
if %errorlevel% neq 0 set /a LPE_FAILED+=1
exit /b 0
