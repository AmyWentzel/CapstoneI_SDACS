param(
    [Parameter(Mandatory = $true)]
    [string]$WifiSsid,

    [Parameter(Mandatory = $true)]
    [string]$WifiPass,

    [Parameter(Mandatory = $true)]
    [string]$MqttUri,

    [Parameter(Mandatory = $true)]
    [string]$MqttTopic,

    [switch]$AlwaysSyncMqtt
)

$ErrorActionPreference = "Stop"

function Escape-CString([string]$Value) {
    return $Value.Replace('\', '\\').Replace('"', '\"')
}

$alwaysSync = if ($AlwaysSyncMqtt.IsPresent) { "1" } else { "0" }
$secretsPath = Join-Path $PSScriptRoot "sdacs_secrets.h"

$content = @"
#pragma once

#define SDACS_SECRET_WIFI_SSID        "$(Escape-CString $WifiSsid)"
#define SDACS_SECRET_WIFI_PASS        "$(Escape-CString $WifiPass)"
#define SDACS_SECRET_MQTT_URI         "$(Escape-CString $MqttUri)"
#define SDACS_SECRET_MQTT_TOPIC       "$(Escape-CString $MqttTopic)"
#define SDACS_SECRET_ALWAYS_SYNC_MQTT $alwaysSync
"@

Set-Content -Path $secretsPath -Value $content -Encoding ascii
Write-Host "Wrote $secretsPath"
Write-Host "Next step: build and flash once to provision these values into NVS."
