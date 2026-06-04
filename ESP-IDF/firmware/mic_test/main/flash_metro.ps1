#call this flash_metro.ps1


param(
    [Parameter(Mandatory=$true)]
    [string]$Port,

    [switch]$Erase
)

$ErrorActionPreference = "Stop"

$ProjectDir = "C:\ws\CapstoneI_SDACS\ESP-IDF\firmware\mic_test"
$Python = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe"
$Esptool = "C:\Espressif\frameworks\esp-idf-v5.3.1\components\esptool_py\esptool\esptool.py"
$IdfPy = "C:\Espressif\frameworks\esp-idf-v5.3.1\tools\idf.py"

function Run-Native {
    param(
        [Parameter(Mandatory=$true)]
        [string]$Exe,

        [Parameter(ValueFromRemainingArguments=$true)]
        [string[]]$Args
    )

    & $Exe @Args

    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Exe $($Args -join ' ')"
    }
}

Set-Location $ProjectDir

Write-Host "Using port $Port"
Write-Host "Put the Metro ESP32-S3 in ROM bootloader mode:"
Write-Host "  Hold BOOT/DFU -> tap Reset -> release BOOT/DFU"
Read-Host "Press Enter once the board is in bootloader mode"

Write-Host "Checking ESP32-S3 connection..."
Run-Native $Python $Esptool --chip esp32s3 -p $Port -b 115200 --before no_reset --after no_reset chip_id

if ($Erase) {
    Write-Host "Erasing flash..."
    Run-Native $Python $Esptool --chip esp32s3 -p $Port -b 115200 --before no_reset --after no_reset erase_flash
}

Write-Host "Building and flashing firmware..."
Run-Native $Python $IdfPy -p $Port -b 115200 flash

Write-Host "Done. Press Reset on the Metro once to run the firmware."