param([string]$Compiler = 'C:\Keil_v5\ARM\ARMCLANG\bin\armclang.exe')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out = Join-Path $PSScriptRoot 'build/arm'
New-Item -ItemType Directory -Force $out | Out-Null
$includes = @('Core/Inc', 'Drivers/STM32F4xx_HAL_Driver/Inc',
    'Drivers/STM32F4xx_HAL_Driver/Inc/Legacy', 'Drivers/CMSIS/Device/ST/STM32F4xx/Include',
    'Drivers/CMSIS/Include', 'USB_DEVICE/App', 'USB_DEVICE/Target',
    'Middlewares/ST/STM32_USB_Device_Library/Core/Inc',
    'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc') |
    ForEach-Object { '-I' + (Join-Path $root $_) }
$sources = @('Core/Src/main.c', 'Core/Src/max31865_driver.c',
    'Core/Src/pt100_acquisition.c', 'Core/Src/pt100_ascii.c', 'Core/Src/pt100_stream.c',
    'Core/Src/pt100_stm32.c', 'Core/Src/pt100_app.c', 'Core/Src/pt100_processing.c', 'Core/Src/pt100_calendar.c', 'Core/Src/pt100_control.c', 'Core/Src/modbus_rtu.c', 'Core/Src/pt100_store.c', 'Core/Src/pt100_flash.c', 'Core/Src/pt100_rtc.c', 'Core/Src/stm32f4xx_hal_msp.c', 'USB_DEVICE/App/usbd_cdc_if.c')
foreach ($source in $sources) {
    $object = Join-Path $out (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    & $Compiler '--target=arm-arm-none-eabi' '-mcpu=cortex-m4' '-mfpu=fpv4-sp-d16' '-mfloat-abi=hard' '-std=c11' '-Wall' '-Wextra' '-Werror' '-DUSE_HAL_DRIVER' '-DSTM32F411xE' @includes '-c' (Join-Path $root $source) '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "ARM compilation failed: $source" }
}
Write-Output 'ARM compilation passed for all changed/new source files. No firmware link performed.'
