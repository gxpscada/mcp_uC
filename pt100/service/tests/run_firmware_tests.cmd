@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
if not exist build mkdir build
cl /nologo /W4 /WX /std:c11 /I..\..\Core\Inc test_firmware.c ..\..\Core\Src\pt100_processing.c ..\..\Core\Src\pt100_calendar.c ..\..\Core\Src\pt100_control.c ..\..\Core\Src\modbus_rtu.c ..\..\Core\Src\pt100_store.c /Febuild\test_firmware.exe /Fobuild\ /Fdbuild\test_firmware.pdb
if errorlevel 1 exit /b 1
build\test_firmware.exe
set result=%errorlevel%
popd
exit /b %result%
