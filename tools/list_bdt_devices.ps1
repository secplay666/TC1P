param(
    [string]$BdtConfigPath = "C:\TelinkIoTStudio\tools\TBD_release\config"
)

$ErrorActionPreference = "Stop"

$bdt = Join-Path $BdtConfigPath "Cmd_download_tool.exe"

if (!(Test-Path -LiteralPath $bdt)) {
    throw "Cmd_download_tool.exe not found: $bdt"
}

& $bdt all
if ($LASTEXITCODE -ne 0) {
    throw "BDT list devices failed with exit code $LASTEXITCODE"
}
