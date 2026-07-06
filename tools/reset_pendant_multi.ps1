param(
    [int]$DeviceId = 1,
    [string]$Chip = "B85",
    [ValidateSet("EVK", "USB")]
    [string]$Transport = "EVK",
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config",
    [string]$DebuggerPid = "",
    [int]$PortNum = -1,
    [int]$HubNum = -1,
    [switch]$SkipActivate
)

$ErrorActionPreference = "Stop"

$bdt = Join-Path $BdtConfigPath "Cmd_download_tool.exe"

if (!(Test-Path -LiteralPath $bdt)) {
    throw "Cmd_download_tool.exe not found: $bdt"
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

if ($Transport -eq "USB") {
    & $bdt $DeviceId $Chip rst -u -f
} else {
    if (!$SkipActivate) {
        & $bdt $bdtDeviceId $Chip ac
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "BDT activate failed with exit code $LASTEXITCODE; continue to reset. Use -SkipActivate to suppress this warning."
        }
    }
    & $bdt $bdtDeviceId $Chip rst -f
}
if ($LASTEXITCODE -ne 0) {
    throw "BDT reset failed with exit code $LASTEXITCODE"
}

Write-Host "Reset OK: DeviceId=$DeviceId Chip=$Chip Transport=$Transport"
