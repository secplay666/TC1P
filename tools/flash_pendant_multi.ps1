param(
    [int]$DeviceId = 1,
    [string]$Chip = "B85",
    [ValidateSet("EVK", "USB")]
    [string]$Transport = "EVK",
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config",
    [string]$BinPath = "",
    [switch]$SkipActivate,
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

if ($Transport -eq "EVK" -and !$SkipActivate) {
    & $bdt $DeviceId $Chip ac
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "BDT activate failed with exit code $LASTEXITCODE; continue to flash. Use -SkipActivate to suppress this warning."
    }
}

if ($Transport -eq "USB") {
    & $bdt $DeviceId $Chip wf 0 -i $BinPath -u
} else {
    & $bdt $DeviceId $Chip wf 0 -i $BinPath
}
if ($LASTEXITCODE -ne 0) {
    throw "BDT flash failed with exit code $LASTEXITCODE"
}

if (!$NoReset) {
    if ($Transport -eq "USB") {
        & $bdt $DeviceId $Chip rst -u -f
    } else {
        & $bdt $DeviceId $Chip rst -f
    }
    if ($LASTEXITCODE -ne 0) {
        throw "BDT reset failed with exit code $LASTEXITCODE"
    }
}
