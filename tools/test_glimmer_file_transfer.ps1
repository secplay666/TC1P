param(
    [string]$SenderDevice = "47162DLAQ003JV",
    [string]$ReceiverDevice = "2KE0219A22011091",
    [string]$TargetShortId = "C4C4154A",
    [int]$Size = 1024,
    [int]$Chunk = 48,
    [int]$DropEvery = 0,
    [int]$DropOnce = -1,
    [int]$WaitMs = 90000,
    [string]$OutputDir = "",
    [switch]$Install,
    [switch]$NoLaunch,
    [string]$ApkPath = "android_glimmer_app\app\build\outputs\apk\debug\app-debug.apk"
)

$ErrorActionPreference = "Stop"

function Convert-ShortId {
    param([Parameter(Mandatory = $true)][string]$Value)

    $text = $Value.Trim()
    if ($text.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
        $text = $text.Substring(2)
    }
    if ($text -match "^[0-9A-Fa-f]{1,8}$" -and ($text -match "[A-Fa-f]" -or $text.Length -eq 8)) {
        return [int64]([Convert]::ToUInt32($text, 16))
    }
    return [int64]$text
}

function Invoke-Adb {
    param(
        [Parameter(Mandatory = $true)][string]$Device,
        [Parameter(Mandatory = $true)][string[]]$Args,
        [switch]$AllowFail
    )

    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & adb -s $Device @Args 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    if ($code -ne 0 -and !$AllowFail) {
        throw "adb -s $Device $($Args -join ' ') failed with code $code`n$output"
    }
    return $output
}

function Start-GlimmerApp {
    param([Parameter(Mandatory = $true)][string]$Device)

    Invoke-Adb -Device $Device -Args @(
        "shell", "am", "start",
        "-n", "com.glimmer.app/.MainActivity"
    ) | Out-Null
}

function Start-GlimmerLogCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Device,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath
    )

    return Start-Process `
        -FilePath "adb" `
        -ArgumentList @("-s", $Device, "logcat", "-v", "time", "-s", "GlimmerFile:I", "GlimmerFile:W", "AndroidRuntime:E") `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -WindowStyle Hidden `
        -PassThru
}

function Stop-GlimmerLogCapture {
    param([Parameter(Mandatory = $true)]$Process)

    if ($Process -and !$Process.HasExited) {
        Stop-Process -Id $Process.Id -Force
        $Process.WaitForExit()
    }
}

function Send-DebugFile {
    param(
        [Parameter(Mandatory = $true)][string]$Device,
        [Parameter(Mandatory = $true)][int64]$TargetShortId,
        [Parameter(Mandatory = $true)][int]$Size,
        [Parameter(Mandatory = $true)][int]$Chunk,
        [Parameter(Mandatory = $true)][int]$DropEvery,
        [Parameter(Mandatory = $true)][int]$DropOnce
    )

    Invoke-Adb -Device $Device -Args @(
        "shell", "am", "broadcast",
        "--receiver-foreground",
        "-a", "com.glimmer.app.DEBUG_FILE",
        "-p", "com.glimmer.app",
        "--el", "short_id", "$TargetShortId",
        "--ei", "size", "$Size",
        "--ei", "chunk", "$Chunk",
        "--ei", "drop_every", "$DropEvery",
        "--ei", "drop_once", "$DropOnce"
    ) | Out-Null
}

function Read-Log {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path $Path) {
        return @(Get-Content $Path)
    }
    return @()
}

function Dump-GlimmerFileLog {
    param([Parameter(Mandatory = $true)][string]$Device)

    $lines = @(Invoke-Adb -Device $Device -Args @("logcat", "-d", "-v", "time") -AllowFail)
    return @($lines | Where-Object { $_ -match "GlimmerFile|AndroidRuntime" })
}

if (!(Get-Command adb -ErrorAction SilentlyContinue)) {
    throw "adb was not found in PATH"
}

if (!$OutputDir) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputDir = Join-Path "artifacts" "glimmer_file_$stamp"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$target = Convert-ShortId $TargetShortId
Write-Host "OutputDir=$OutputDir"
Write-Host "Sender=$SenderDevice Receiver=$ReceiverDevice Target=$TargetShortId/$target Size=$Size Chunk=$Chunk DropEvery=$DropEvery DropOnce=$DropOnce"

if ($Install) {
    if (!(Test-Path $ApkPath)) {
        throw "APK not found: $ApkPath"
    }
    Invoke-Adb -Device $SenderDevice -Args @("install", "-r", $ApkPath) | Out-Host
    Invoke-Adb -Device $ReceiverDevice -Args @("install", "-r", $ApkPath) | Out-Host
}

Invoke-Adb -Device $SenderDevice -Args @("logcat", "-c") | Out-Null
Invoke-Adb -Device $ReceiverDevice -Args @("logcat", "-c") | Out-Null

$senderLogPath = Join-Path $OutputDir "sender.log"
$receiverLogPath = Join-Path $OutputDir "receiver.log"
$senderErrPath = Join-Path $OutputDir "sender.log.err"
$receiverErrPath = Join-Path $OutputDir "receiver.log.err"
$senderLogProcess = Start-GlimmerLogCapture -Device $SenderDevice -StdoutPath $senderLogPath -StderrPath $senderErrPath
$receiverLogProcess = Start-GlimmerLogCapture -Device $ReceiverDevice -StdoutPath $receiverLogPath -StderrPath $receiverErrPath
Start-Sleep -Milliseconds 800

if (!$NoLaunch) {
    Start-GlimmerApp -Device $SenderDevice
    Start-GlimmerApp -Device $ReceiverDevice
    Start-Sleep -Milliseconds 5000
}

Send-DebugFile -Device $SenderDevice -TargetShortId $target -Size $Size -Chunk $Chunk -DropEvery $DropEvery -DropOnce $DropOnce

Write-Host "Waiting $WaitMs ms for file transfer..."
Start-Sleep -Milliseconds $WaitMs

Stop-GlimmerLogCapture -Process $senderLogProcess
Stop-GlimmerLogCapture -Process $receiverLogProcess
Start-Sleep -Milliseconds 500

$senderLog = Dump-GlimmerFileLog -Device $SenderDevice
$receiverLog = Dump-GlimmerFileLog -Device $ReceiverDevice
$senderLog | Set-Content -Encoding UTF8 -Path $senderLogPath
$receiverLog | Set-Content -Encoding UTF8 -Path $receiverLogPath
$allLog = @($senderLog + $receiverLog)
$allLog | Set-Content -Encoding UTF8 -Path (Join-Path $OutputDir "combined.log")

$rxComplete = @($receiverLog | Where-Object { $_ -match "rx_complete .* ok=1" })
$peerDone = @($senderLog | Where-Object { $_ -match "peer_done .* status=0" })
$txComplete = @($senderLog | Where-Object { $_ -match "tx_complete |tx_complete_by_done " })
$repair = @($senderLog | Where-Object { $_ -match "\brepair file=" })
$abort = @($allLog | Where-Object { $_ -match "tx_abort|start_failed|rx_start_reject" })
$confirmed = ($rxComplete.Count -gt 0) -or ($peerDone.Count -gt 0) -or ($txComplete.Count -gt 0)

Write-Host "rx_complete_ok=$($rxComplete.Count)"
Write-Host "peer_done_ok=$($peerDone.Count)"
Write-Host "tx_complete=$($txComplete.Count)"
Write-Host "repair_rounds=$($repair.Count)"
Write-Host "abort_or_reject=$($abort.Count)"

if (!$confirmed -or $abort.Count -gt 0) {
    Write-Host "---- sender tail ----"
    $senderLog | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
    Write-Host "---- receiver tail ----"
    $receiverLog | Select-Object -Last 40 | ForEach-Object { Write-Host $_ }
    throw "Glimmer file transfer test failed; see $OutputDir"
}

Write-Host "Glimmer file transfer test passed; see $OutputDir"
