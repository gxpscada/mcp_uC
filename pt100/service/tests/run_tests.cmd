@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /W4 /WX /std:c11 /I..\..\Core\Inc test_stage2.c ..\..\Core\Src\max31865_driver.c ..\..\Core\Src\pt100_acquisition.c ..\..\Core\Src\pt100_ascii.c ..\..\Core\Src\pt100_stream.c /Febuild\test_stage2.exe /Fobuild\ /Fdbuild\test_stage2.pdb
if errorlevel 1 exit /b 1
build\test_stage2.exe
set result=%errorlevel%
popd
exit /b %result%
