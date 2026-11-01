param(
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

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$bdt = Join-Path $BdtConfigPath "Cmd_download_tool.exe"

if ([string]::IsNullOrWhiteSpace($BinPath)) {
    $BinPath = Join-Path $repoRoot "tc_ble_multi_sdk\build\B85\pendant\pendant.bin"
}

if (!(Test-Path -LiteralPath $bdt)) {
    throw "Cmd_download_tool.exe not found: $bdt"
}

if (!(Test-Path -LiteralPath $BinPath)) {
    throw "Firmware bin not found: $BinPath"
}

if ($Transport -eq "EVK") {
    . (Join-Path $PSScriptRoot "bdt_common.ps1")
    $selection = Resolve-BdtEvkPortNum -Bdt $bdt -BoardId $DeviceId -DebuggerPid $DebuggerPid -PortNum $PortNum -HubNum $HubNum
    if ($env:BDT_DEBUG_LIST -and $null -ne $selection.List) {
        Write-Host "BDT device list:"
        Write-Host $selection.List.Raw
    }
    $bdtDeviceId = $selection.BdtDeviceId
    Write-Host "BDT select: RequestedId=$DeviceId Source=$($selection.Source) Pid=$($selection.DebuggerPid) BdtDeviceId=$bdtDeviceId PortNum=$($selection.PortNum) HubNum=$($selection.HubNum)"
}

if ($Transport -eq "EVK" -and !$SkipActivate) {
    # Firmware with BLE power management suspends between radio events, and
    # Swire cannot hold a suspended MCU. Reset first so activate lands in the
    # firmware's awake window after boot, then confirm the core is halted.
    $halted = $false
    for ($attempt = 1; $attempt -le 5 -and !$halted; $attempt++) {
        & $bdt $bdtDeviceId $Chip rst -f | Out-Null
        & $bdt $bdtDeviceId $Chip ac | Out-Null
        $probe = & $bdt $bdtDeviceId $Chip rf 0 -s 4 2>&1 | Out-String
        $halted = $probe -match "00000000:"
    }
    if (!$halted) {
        Write-Warning "BDT could not halt the MCU after 5 reset+activate attempts; trying to flash anyway."
    }
}

if ($Transport -eq "USB") {
    & $bdt $DeviceId $Chip wf 0 -i $BinPath -u
} else {
    & $bdt $bdtDeviceId $Chip wf 0 -i $BinPath
}
if ($LASTEXITCODE -ne 0) {
    throw "BDT flash failed with exit code $LASTEXITCODE"
}

if (!$NoReset) {
    if ($Transport -eq "USB") {
        & $bdt $DeviceId $Chip rst -u -f
    } else {
        & $bdt $bdtDeviceId $Chip rst -f
    }
    if ($LASTEXITCODE -ne 0) {
        throw "BDT reset failed with exit code $LASTEXITCODE"
    }
}
