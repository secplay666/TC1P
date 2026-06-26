param(
    [ValidateSet("1", "2", "3", "4", "BOARD1", "BOARD2", "BOARD3", "BOARD4", "COM9", "COM10", "COM16", "COM17", "A", "B", "ON", "OFF")]
    [string]$Target = "1",
    [string]$RelayPort = "COM18",
    [int]$RelayBaudRate = 9600,
    [int]$RelaySettleMs = 700,
    [int]$DeviceId = 1,
    [string]$Chip = "B85",
    [ValidateSet("EVK", "USB")]
    [string]$Transport = "EVK",
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config",
    [string]$BinPath = "",
    [switch]$SkipActivate,
    [switch]$NoReset,
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

$flashArgs = @{
    DeviceId      = $DeviceId
    Chip          = $Chip
    Transport     = $Transport
    BdtConfigPath = $BdtConfigPath
    BinPath       = $BinPath
    DebuggerPid   = $DebuggerPid
    PortNum       = $PortNum
    HubNum        = $HubNum
}

if ($SkipActivate) {
    $flashArgs.SkipActivate = $true
}
if ($NoReset) {
    $flashArgs.NoReset = $true
}

& (Join-Path $PSScriptRoot "flash_pendant_multi.ps1") @flashArgs
