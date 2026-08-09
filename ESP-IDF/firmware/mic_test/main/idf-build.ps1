param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$idfPath = "C:\Espressif\frameworks\esp-idf-v5.3.1"
$idfPy = Join-Path $idfPath "tools\idf.py"
$pythonExe = "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe"

if (-not (Test-Path $idfPy)) {
    throw "ESP-IDF tool not found: $idfPy"
}

if (-not (Test-Path $pythonExe)) {
    throw "ESP-IDF Python not found: $pythonExe"
}

if (-not $IdfArgs -or $IdfArgs.Count -eq 0) {
    $IdfArgs = @("build")
}

Push-Location $projectRoot
try {
    & $pythonExe $idfPy @IdfArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
