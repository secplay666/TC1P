param(
    [string]$TelinkStudioPath = "C:\TelinkIoTStudio",
    [string]$WorkspacePath = ".telink_workspace",
    [switch]$KeepWorkspace,
    [switch]$Headless,
    [switch]$Clean,
    [int]$VersionCode = 120
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
$buildInfoScript = Join-Path $PSScriptRoot "update_pendant_build_info.ps1"

if (!(Test-Path -LiteralPath (Join-Path $projectDir ".project"))) {
    throw "Telink project not found: $projectDir"
}

# The generated makefiles do not track header dependencies (no .d files), so a
# changed header (e.g. app_config.h) would leave stale objects behind. Hash all
# headers and force a clean build whenever they differ from the last build.
# app_build_info.h is excluded: it changes every build and its users are
# rebuilt explicitly below.
$sdkRoot = Join-Path $repoRoot "tc_ble_multi_sdk"
$sdkBuildRoot = Join-Path $sdkRoot "build"
$headerHashFile = Join-Path $buildDir ".header_hash"
$headerFiles = Get-ChildItem -LiteralPath $sdkRoot -Recurse -File -Filter "*.h" |
    Where-Object {
        !$_.FullName.StartsWith($sdkBuildRoot, [System.StringComparison]::OrdinalIgnoreCase) -and
        $_.Name -ne "app_build_info.h"
    } |
    Sort-Object FullName
$sha = [System.Security.Cryptography.SHA256]::Create()
$hashInput = New-Object System.IO.MemoryStream
foreach ($header in $headerFiles) {
    $nameBytes = [System.Text.Encoding]::UTF8.GetBytes($header.FullName.Substring($sdkRoot.Length))
    $hashInput.Write($nameBytes, 0, $nameBytes.Length)
    $contentBytes = [System.IO.File]::ReadAllBytes($header.FullName)
    $hashInput.Write($contentBytes, 0, $contentBytes.Length)
}
$headerHash = [System.BitConverter]::ToString($sha.ComputeHash($hashInput.ToArray())).Replace("-", "")
$lastHeaderHash = if (Test-Path -LiteralPath $headerHashFile) { (Get-Content -LiteralPath $headerHashFile -Raw).Trim() } else { "" }
if (!$Clean -and $headerHash -ne $lastHeaderHash) {
    Write-Host "Header files changed since last build (or no record); forcing clean build."
    $Clean = $true
}

if (Test-Path -LiteralPath $buildInfoScript) {
    & $buildInfoScript -RepoRoot $repoRoot -VersionCode $VersionCode
    $buildInfoUsers = @(
        "vendor\pendant\app_ota.o",
        "vendor\pendant\host_cmd\app_host_cmd.o",
        "vendor\pendant\debug_shell\app_debug_shell_cmd.o"
    )
    foreach ($relativePath in $buildInfoUsers) {
        $buildInfoUser = Join-Path $buildDir $relativePath
        if (Test-Path -LiteralPath $buildInfoUser) {
            Remove-Item -LiteralPath $buildInfoUser -Force
        }
    }
}

if ($Headless) {
    if (!(Test-Path -LiteralPath $studioConsole)) {
        throw "TelinkIoTStudioc.exe not found: $studioConsole"
    }

    if ($Clean -and !$KeepWorkspace -and (Test-Path -LiteralPath $workspace)) {
        $resolvedWorkspace = (Resolve-Path -LiteralPath $workspace).Path
        if (!$resolvedWorkspace.StartsWith($repoRoot)) {
            throw "Refusing to delete workspace outside repo: $resolvedWorkspace"
        }
        Remove-Item -LiteralPath $resolvedWorkspace -Recurse -Force
    }

    $headlessBuildAction = if ($Clean) { "-cleanBuild" } else { "-build" }
    & $studioConsole `
        -nosplash `
        -no-indexer `
        -application org.eclipse.cdt.managedbuilder.core.headlessbuild `
        -data $workspace `
        -import $projectDir `
        $headlessBuildAction "tc_ble_multi_sdk_B85/pendant"

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
        if ($Clean) {
            & $makePath MAKE=make -j1 clean all
        } else {
            & $makePath MAKE=make -j1 all
        }
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

Set-Content -LiteralPath $headerHashFile -Value $headerHash -NoNewline

$bin = Get-Item -LiteralPath $outputBin
Write-Host "Built: $($bin.FullName) ($($bin.Length) bytes)"
