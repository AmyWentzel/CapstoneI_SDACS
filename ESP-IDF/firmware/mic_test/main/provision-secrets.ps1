param(
    [Parameter(Mandatory = $false)]
    [string]$WifiSsid,

    [Parameter(Mandatory = $false)]
    [string]$WifiPass,

    [Parameter(Mandatory = $false)]
    [string]$MqttUri,

    [Parameter(Mandatory = $false)]
    [string]$MqttTopic,

    [Parameter(Mandatory = $true)]
    [ValidatePattern('^node0[1-4]$')]
    [string]$NodeId,

    [switch]$AlwaysSyncMqtt
)

$ErrorActionPreference = "Stop"

function Escape-CString([string]$Value) {
    return $Value.Replace('\', '\\').Replace('"', '\"')
}

function Read-ExistingDefine([string]$Path, [string]$Name, [string]$DefaultValue) {
    if (-not (Test-Path $Path)) {
        return $DefaultValue
    }

    $pattern = '^\s*#define\s+' + [regex]::Escape($Name) + '\s+"(.*)"\s*$'
    foreach ($line in Get-Content -Path $Path) {
        $match = [regex]::Match($line, $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value.Replace('\"', '"').Replace('\\', '\')
        }
    }

    return $DefaultValue
}

function Read-ExistingNumericDefine([string]$Path, [string]$Name, [string]$DefaultValue) {
    if (-not (Test-Path $Path)) {
        return $DefaultValue
    }

    $pattern = '^\s*#define\s+' + [regex]::Escape($Name) + '\s+([0-9]+)\s*$'
    foreach ($line in Get-Content -Path $Path) {
        $match = [regex]::Match($line, $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value
        }
    }

    return $DefaultValue
}

$alwaysSync = if ($AlwaysSyncMqtt.IsPresent) { "1" } else { "0" }
$secretsPath = Join-Path $PSScriptRoot "sdacs_secrets.h"

if (-not $PSBoundParameters.ContainsKey("WifiSsid")) {
    $WifiSsid = Read-ExistingDefine $secretsPath "SDACS_SECRET_WIFI_SSID" ""
}
if (-not $PSBoundParameters.ContainsKey("WifiPass")) {
    $WifiPass = Read-ExistingDefine $secretsPath "SDACS_SECRET_WIFI_PASS" ""
}
if (-not $PSBoundParameters.ContainsKey("MqttUri")) {
    $MqttUri = Read-ExistingDefine $secretsPath "SDACS_SECRET_MQTT_URI" ""
}
if (-not $PSBoundParameters.ContainsKey("AlwaysSyncMqtt")) {
    $alwaysSync = Read-ExistingNumericDefine $secretsPath "SDACS_SECRET_ALWAYS_SYNC_MQTT" "0"
}

# Node-specific MQTT topics are deprecated. Runtime topics are built from NodeId.
$MqttTopic = ""

$content = @"
#pragma once

#define SDACS_SECRET_WIFI_SSID        "$(Escape-CString $WifiSsid)"
#define SDACS_SECRET_WIFI_PASS        "$(Escape-CString $WifiPass)"
#define SDACS_SECRET_MQTT_URI         "$(Escape-CString $MqttUri)"
#define SDACS_SECRET_MQTT_TOPIC       "$(Escape-CString $MqttTopic)"
#define SDACS_SECRET_ALWAYS_SYNC_MQTT $alwaysSync
#define SDACS_SECRET_NODE_ID          "$(Escape-CString $NodeId)"
"@

Set-Content -Path $secretsPath -Value $content -Encoding ascii
Write-Host "Wrote $secretsPath"
Write-Host "Compiled Node ID: $NodeId"
Write-Host "Expected BLE name: SDACS-$NodeId"
Write-Host "Expected MQTT base: sdacs/node/$NodeId"
