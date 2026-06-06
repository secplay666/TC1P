param(
    [string]$TelinkStudioPath = "C:\TelinkIoTStudio",
    [string]$WorkspacePath = ".telink_workspace",
    [switch]$KeepWorkspace,
    [switch]$Headless
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$projectDir = Join-Path $repoRoot "tc_ble_multi_sdk\build\B85"
$buildDir = Join-Path $projectDir "pendant"
$workspace = Join-Path $repoRoot $WorkspacePath
$studioConsole = Join-Path $TelinkStudioPath "TelinkIoTStudioc.exe"
$makePath = Join-Path $TelinkStudioPath "bin\make.exe"
$toolchainBin = Join-Path $TelinkStudioPath "opt\tc32\bin"
$studioBin = Join-Path $TelinkStudioPath "bin"
$outputBin = Join-Path $projectDir "pendant\pendant.bin"

if (!(Test-Path -LiteralPath (Join-Path $projectDir ".project"))) {
    throw "Telink project not found: $projectDir"
}

if ($Headless) {
    if (!(Test-Path -LiteralPath $studioConsole)) {
        throw "TelinkIoTStudioc.exe not found: $studioConsole"
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
} else {
    if (!(Test-Path -LiteralPath $makePath)) {
        throw "make.exe not found: $makePath"
    }
    if (!(Test-Path -LiteralPath $toolchainBin)) {
        throw "TC32 toolchain not found: $toolchainBin"
    }
    if (!(Test-Path -LiteralPath $buildDir)) {
        throw "Build directory not found: $buildDir"
    }

    $env:Path = "$toolchainBin;$studioBin;" + $env:Path
    Push-Location -LiteralPath $buildDir
    try {
        & $makePath MAKE=make -j1 clean all
        if ($LASTEXITCODE -ne 0) {
            throw "make failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

if (!(Test-Path -LiteralPath $outputBin)) {
    throw "Build succeeded but output bin was not found: $outputBin"
}

$bin = Get-Item -LiteralPath $outputBin
Write-Host "Built: $($bin.FullName) ($($bin.Length) bytes)"
