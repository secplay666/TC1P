param(
    [ValidateSet("COM16", "COM17", "A", "B", "ON", "OFF")]
    [string]$Target = "COM16",
    [string]$RelayPort = "COM18",
    [int]$RelayBaudRate = 9600,
    [int]$RelaySettleMs = 700,
    [int]$DeviceId = 1,
    [string]$Chip = "B85",
    [ValidateSet("EVK", "USB")]
    [string]$Transport = "EVK",
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config",
    [string]$DebuggerPid = "",
    [int]$PortNum = -1,
    [int]$HubNum = -1
)

$ErrorActionPreference = "Stop"

& (Join-Path $PSScriptRoot "relay_select_b85.ps1") `
    -Target $Target `
    -RelayPort $RelayPort `
    -BaudRate $RelayBaudRate `
    -SettleMs $RelaySettleMs

& (Join-Path $PSScriptRoot "reset_pendant_multi.ps1") `
    -DeviceId $DeviceId `
    -Chip $Chip `
    -Transport $Transport `
    -BdtConfigPath $BdtConfigPath `
    -DebuggerPid $DebuggerPid `
    -PortNum $PortNum `
    -HubNum $HubNum
