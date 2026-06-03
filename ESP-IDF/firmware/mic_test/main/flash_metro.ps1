# flash_metro.ps1
param(
    [Parameter(Mandatory=$true)]
    [string]$Port,

    [Parameter(Mandatory=$true)]
    [ValidatePattern('^node0[1-4]$')]
    [string]$NodeId,

    [switch]$Erase
)

$ErrorActionPreference = "Stop"

$ProjectDir = "C:\ws\CapstoneI_SDACS\ESP-IDF\firmware\mic_test"
$Python = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe"
$IdfPy = "C:\Espressif\frameworks\esp-idf-v5.3.1\tools\idf.py"
$Esptool = "C:\Espressif\frameworks\esp-idf-v5.3.1\components\esptool_py\esptool\esptool.py"

$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.3.1"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.3_py3.11_env"
$idfTools = @(
    "C:\Espressif\tools\xtensa-esp-elf-gdb\14.2_20240403\xtensa-esp-elf-gdb\bin",
    "C:\Espressif\tools\riscv32-esp-elf-gdb\14.2_20240403\riscv32-esp-elf-gdb\bin",
    "C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin",
    "C:\Espressif\tools\esp-clang\16.0.1-fe4f10a809\esp-clang\bin",
    "C:\Espressif\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin",
    "C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
    "C:\Espressif\tools\cmake\3.24.0\bin",
    "C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20240318\openocd-esp32\bin",
    "C:\Espressif\tools\ninja\1.11.1",
    "C:\Espressif\tools\idf-exe\1.0.3",
    "C:\Espressif\tools\ccache\4.8\ccache-4.8-windows-x86_64",
    "C:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64",
    "C:\Espressif\frameworks\esp-idf-v5.3.1\tools"
)
$env:PATH = ($idfTools -join ";") + ";" + $env:PATH

Set-Location $ProjectDir

Write-Host "Using port $Port"
Write-Host "Provisioning compiled identity $NodeId"
& (Join-Path $PSScriptRoot "provision-secrets.ps1") -NodeId $NodeId

Write-Host "Building firmware for $NodeId..."
& $Python $IdfPy build

Write-Host "Put the Metro ESP32-S3 in ROM bootloader mode now:"
Write-Host "  Hold BOOT/DFU -> tap Reset -> release BOOT/DFU"
Read-Host "Press Enter once the board is in bootloader mode"

Write-Host "Checking ESP32-S3 connection..."
& $Python $Esptool --chip esp32s3 -p $Port -b 115200 --before no_reset --after no_reset chip_id

if ($Erase) {
    Write-Host "Erasing flash..."
    & $Python $Esptool --chip esp32s3 -p $Port -b 115200 --before no_reset --after no_reset erase_flash
}

Write-Host "Flashing firmware..."
& $Python $IdfPy -p $Port -b 115200 flash

Write-Host "Flashed $NodeId."
Write-Host "Expected BLE name: SDACS-$NodeId"
Write-Host "Expected MQTT base: sdacs/node/$NodeId"
Write-Host "Done. Press Reset on the Metro once to run the firmware."
