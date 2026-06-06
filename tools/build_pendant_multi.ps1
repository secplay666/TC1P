param(
    [string]$TelinkStudioPath = "C:\TelinkIoTStudio",
    [string]$WorkspacePath = ".telink_workspace",
    [switch]$KeepWorkspace
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$projectDir = Join-Path $repoRoot "tc_ble_multi_sdk\build\B85"
$workspace = Join-Path $repoRoot $WorkspacePath
$studioConsole = Join-Path $TelinkStudioPath "TelinkIoTStudioc.exe"
$outputBin = Join-Path $projectDir "pendant\pendant.bin"

if (!(Test-Path -LiteralPath $studioConsole)) {
    throw "TelinkIoTStudioc.exe not found: $studioConsole"
}

if (!(Test-Path -LiteralPath (Join-Path $projectDir ".project"))) {
    throw "Telink project not found: $projectDir"
}

if (!$KeepWorkspace -and (Test-Path -LiteralPath $workspace)) {
    $resolvedWorkspace = (Resolve-Path -LiteralPath $workspace).Path
    if (!$resolvedWorkspace.StartsWith($repoRoot)) {
        throw "Refusing to delete workspace outside repo: $resolvedWorkspace"
    }
    Remove-Item -LiteralPath $resolvedWorkspace -Recurse -Force
}

& $studioConsole `
    -nosplash `
    -no-indexer `
    -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
    -data $workspace `
    -import $projectDir `
    -cleanBuild "tc_ble_multi_sdk_B85/pendant"

if ($LASTEXITCODE -ne 0) {
    throw "Telink headless build failed with exit code $LASTEXITCODE"
}

if (!(Test-Path -LiteralPath $outputBin)) {
    throw "Build succeeded but output bin was not found: $outputBin"
}

$bin = Get-Item -LiteralPath $outputBin
Write-Host "Built: $($bin.FullName) ($($bin.Length) bytes)"
