param([string]$ToolchainBin = '')
# Compatibility entry point: the current firmware is v2, Modbus, three channels.
& (Join-Path $PSScriptRoot 'build_firmware.ps1') -ToolchainBin $ToolchainBin -Target F411
