param([string]$ToolchainBin = '', [ValidateSet('F411','F407')][string]$Target='F411', [ValidateRange(1,8)][int]$Sensors=3)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $ToolchainBin -and (Test-Path (Join-Path $projectRoot 'service/local/toolchains/arm-gnu/bin/arm-none-eabi-gcc.exe'))) { $ToolchainBin = [IO.Path]::GetFullPath((Join-Path $projectRoot 'service/local/toolchains/arm-gnu/bin')) }
$compiler = if ($ToolchainBin) { Join-Path $ToolchainBin 'arm-none-eabi-gcc.exe' } else { 'arm-none-eabi-gcc' }
if (-not (Get-Command $compiler -ErrorAction SilentlyContinue)) {
    throw 'ARM GNU toolchain not found. Pass -ToolchainBin <directory containing arm-none-eabi-gcc.exe>, or build the project in STM32CubeIDE.'
}
$suffix = if ($Sensors -eq 3) { '' } else { '-' + $Sensors + 'ch' }
$outputDir = Join-Path $projectRoot ('service/local/build-' + $Target.ToLower() + $suffix)
New-Item -ItemType Directory -Force $outputDir | Out-Null
$includes = @('Core/Inc', 'Drivers/STM32F4xx_HAL_Driver/Inc',
    'Drivers/STM32F4xx_HAL_Driver/Inc/Legacy', 'Drivers/CMSIS/Device/ST/STM32F4xx/Include',
    'Drivers/CMSIS/Include', 'USB_DEVICE/App', 'USB_DEVICE/Target',
    'Middlewares/ST/STM32_USB_Device_Library/Core/Inc',
    'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc') |
    ForEach-Object { '-I' + (Join-Path $projectRoot $_) }
$flags = @('-mcpu=cortex-m4', '-mthumb', '-mfpu=fpv4-sp-d16', '-mfloat-abi=hard',
    '-DUSE_HAL_DRIVER', ($(if ($Target -eq 'F407') {'-DSTM32F407xx'} else {'-DSTM32F411xE'})), ($(if ($Target -eq 'F407') {'-DHSE_VALUE=8000000'} else {'-DHSE_VALUE=25000000'})), ('-DPT100_BOARD_CHANNEL_COUNT='+$Sensors), ('-DPT100_DEFAULT_CHANNELS='+$Sensors), '-g3', '-Og', '-ffunction-sections', '-fdata-sections')
$sources = @('Core/Src', 'Drivers/STM32F4xx_HAL_Driver/Src',
    'USB_DEVICE/App', 'USB_DEVICE/Target',
    'Middlewares/ST/STM32_USB_Device_Library/Core/Src',
    'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src') |
    ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $projectRoot $_) -Filter '*.c' }
$objects = @()
foreach ($source in $sources) {
    $object = Join-Path $outputDir ($source.BaseName + '.o')
    $strict = @()
    if ($source.FullName -match '\\Core\\|usbd_cdc_if\.c$') { $strict += '-Werror' }
    & $compiler @flags @includes @strict '-std=gnu11' '-Wall' '-Wextra' '-c' $source.FullName '-o' $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $($source.Name)" }
    $objects += $object
}
$startup = Join-Path $outputDir 'startup.o'
& $compiler @flags '-x' 'assembler-with-cpp' '-c' (Join-Path $projectRoot $(if ($Target -eq 'F407') {'targets/f407/startup_stm32f407xx.s'} else {'Core/Startup/startup_stm32f411ceux.s'})) '-o' $startup
if ($LASTEXITCODE -ne 0) { throw 'Startup assembly failed' }
$objects += $startup
$elf = Join-Path $outputDir 'pt100.elf'
& $compiler @flags @objects ('-T' + (Join-Path $projectRoot 'STM32F411CEUX_FLASH.ld')) '--specs=nosys.specs' '--specs=nano.specs' '-Wl,--gc-sections' ('-Wl,-Map=' + (Join-Path $outputDir 'pt100.map')) '-Wl,--start-group' '-lc' '-lm' '-Wl,--end-group' '-o' $elf
if ($LASTEXITCODE -ne 0) { throw 'Link failed' }
$objcopy = if ($ToolchainBin) { Join-Path $ToolchainBin 'arm-none-eabi-objcopy.exe' } else { 'arm-none-eabi-objcopy' }
& $objcopy '-O' 'ihex' $elf (Join-Path $outputDir 'pt100.hex')
if ($LASTEXITCODE -ne 0) { throw 'HEX conversion failed' }
$sizeTool = if ($ToolchainBin) { Join-Path $ToolchainBin 'arm-none-eabi-size.exe' } else { 'arm-none-eabi-size' }
& $sizeTool $elf
if ($LASTEXITCODE -ne 0) { throw 'Size inspection failed' }
@{ target=$Target; channels=$Sensors; utc=[DateTime]::UtcNow.ToString('o'); elfSha256=(Get-FileHash $elf -Algorithm SHA256).Hash } |
    ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $outputDir 'build-info.json')
Write-Output "Built: $elf"
