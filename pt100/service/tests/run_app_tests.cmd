@echo off
setlocal
where cl >nul 2>nul
if errorlevel 1 call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
set channels=%~1
if not defined channels set channels=3
if not exist build mkdir build
cl /nologo /W4 /WX /std:c11 /DSTM32F411xE /DPT100_BOARD_CHANNEL_COUNT=%channels% /DPT100_DEFAULT_CHANNELS=%channels% /Ifakes /I..\..\Core\Inc test_app.c ..\..\Core\Src\pt100_app.c ..\..\Core\Src\pt100_stm32.c ..\..\Core\Src\max31865_driver.c ..\..\Core\Src\pt100_acquisition.c ..\..\Core\Src\pt100_processing.c ..\..\Core\Src\pt100_calendar.c ..\..\Core\Src\pt100_control.c ..\..\Core\Src\modbus_rtu.c ..\..\Core\Src\pt100_store.c /Febuild\test_app.exe /Fobuild\ /Fdbuild\test_app.pdb
if errorlevel 1 exit /b 1
build\test_app.exe
set result=%errorlevel%
popd
exit /b %result%
