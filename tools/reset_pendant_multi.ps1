param(
    [int]$DeviceId = 1,
    [string]$Chip = "B85",
    [ValidateSet("EVK", "USB")]
    [string]$Transport = "EVK",
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config"
)

$ErrorActionPreference = "Stop"

$bdt = Join-Path $BdtConfigPath "Cmd_download_tool.exe"

if (!(Test-Path -LiteralPath $bdt)) {
    throw "Cmd_download_tool.exe not found: $bdt"
}

if ($Transport -eq "USB") {
    & $bdt $DeviceId $Chip rst -u -f
} else {
    & $bdt $DeviceId $Chip rst -f
}
if ($LASTEXITCODE -ne 0) {
    throw "BDT reset failed with exit code $LASTEXITCODE"
}

Write-Host "Reset OK: DeviceId=$DeviceId Chip=$Chip Transport=$Transport"
