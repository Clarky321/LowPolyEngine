@echo off
rem Run a built executable from build\<preset>\bin\<Config>\.
rem Usage: run.bat [debug|release] [target]   (defaults: debug, island-preview)
rem Example: run.bat debug lpe_unit_tests
rem The working directory is the repository root, so development builds can
rem find files under assets\ the same way regardless of where this is called from.
setlocal

call "%~dp0_common.bat" :resolve_config %1
if %errorlevel% neq 0 exit /b %errorlevel%

set "LPE_TARGET=%~2"
if "%LPE_TARGET%"=="" set "LPE_TARGET=island-preview"
set "LPE_EXE=%LPE_BUILD_DIR%\bin\%LPE_CONFIG%\%LPE_TARGET%.exe"
if not exist "%LPE_EXE%" goto :not_built

pushd "%LPE_ROOT%"
"%LPE_EXE%"
set "LPE_EXIT=%errorlevel%"
popd
exit /b %LPE_EXIT%

:not_built
echo error: "%LPE_EXE%" does not exist. 1>&2
echo Build it first: scripts\windows\build.bat %LPE_CONFIG% 1>&2
echo If it is still missing after a build, this checkout has no "%LPE_TARGET%" target. 1>&2
exit /b 1
