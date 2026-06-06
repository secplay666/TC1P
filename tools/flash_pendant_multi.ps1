param(
    [int]$DeviceId = 1,
    [string]$Chip = "B85",
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config",
    [string]$BinPath = "",
    [switch]$NoReset
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

& $bdt $DeviceId $Chip wf 0 -i $BinPath
if ($LASTEXITCODE -ne 0) {
    throw "BDT flash failed with exit code $LASTEXITCODE"
}

if (!$NoReset) {
    & $bdt $DeviceId $Chip rst -f
    if ($LASTEXITCODE -ne 0) {
        throw "BDT reset failed with exit code $LASTEXITCODE"
    }
}
