param(
    [string]$PixelDevice = "47162DLAQ003JV",
    [string]$HuaweiDevice = "2KE0219A22011091",
    [string]$PixelTargetShortId = "C4C4154A",
    [string]$HuaweiTargetShortId = "6B463997",
    [int]$CountPerSide = 8,
    [int]$MinDelayMs = 900,
    [int]$MaxDelayMs = 2200,
    [int]$FinalWaitMs = 35000,
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
    if ($text -match "^[0-9A-Fa-f]{1,8}$" -and $text -match "[A-Fa-f]") {
        return [int64]([Convert]::ToUInt32($text, 16))
    }
    if ($text -match "^[0-9]{1,8}$" -and $text.Length -eq 8) {
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

function Send-DebugChat {
    param(
        [Parameter(Mandatory = $true)][string]$Device,
        [Parameter(Mandatory = $true)][int64]$TargetShortId,
        [Parameter(Mandatory = $true)][string]$Text
    )

    Invoke-Adb -Device $Device -Args @(
        "shell", "am", "broadcast",
        "--receiver-foreground",
        "-a", "com.glimmer.app.DEBUG_CHAT",
        "-p", "com.glimmer.app",
        "--el", "short_id", "$TargetShortId",
        "--es", "text", $Text,
        "--es", "dir", "real"
    ) | Out-Null
}

function Read-GlimmerLog {
    param([Parameter(Mandatory = $true)][string]$Device)

    return @(Invoke-Adb -Device $Device -Args @("logcat", "-d", "-v", "time", "-s", "GlimmerChat:I", "AndroidRuntime:E") -AllowFail)
}

function Start-GlimmerLogCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Device,
        [Parameter(Mandatory = $true)][string]$StdoutPath,
        [Parameter(Mandatory = $true)][string]$StderrPath
    )

    return Start-Process `
        -FilePath "adb" `
        -ArgumentList @("-s", $Device, "logcat", "-v", "time", "-s", "GlimmerChat:I", "AndroidRuntime:E") `
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

function Parse-GlimmerChatLog {
    param(
        [Parameter(Mandatory = $true)][string]$DeviceName,
        [string[]]$Lines = @()
    )

    $rows = New-Object System.Collections.Generic.List[object]
    foreach ($line in $Lines) {
        if ($line -match "enqueue local=(\d+) dst=([0-9A-F]+) text=(\S+)") {
            $rows.Add([pscustomobject]@{
                Device = $DeviceName
                Event = "enqueue"
                Local = [int]$matches[1]
                Seq = ""
                Peer = $matches[2]
                Host = ""
                App = ""
                Status = ""
                Text = $matches[3]
                Raw = $line
            })
        } elseif ($line -match "dispatch local=(\d+) seq=(\d+) dst=([0-9A-F]+) text=(\S+)") {
            $rows.Add([pscustomobject]@{
                Device = $DeviceName
                Event = "dispatch"
                Local = [int]$matches[1]
                Seq = [int]$matches[2]
                Peer = $matches[3]
                Host = ""
                App = ""
                Status = ""
                Text = $matches[4]
                Raw = $line
            })
        } elseif ($line -match "finish local=(\d+) seq=(\d+) dst=([0-9A-F]+) status=(.+?) text=(\S+)") {
            $rows.Add([pscustomobject]@{
                Device = $DeviceName
                Event = "finish"
                Local = [int]$matches[1]
                Seq = [int]$matches[2]
                Peer = $matches[3]
                Host = ""
                App = ""
                Status = $matches[4]
                Text = $matches[5]
                Raw = $line
            })
        } elseif ($line -match "tx_result short=([0-9A-F]+) host=(\d+) app=(\d+) peerMsg=(\d+)") {
            $rows.Add([pscustomobject]@{
                Device = $DeviceName
                Event = "tx_result"
                Local = ""
                Seq = ""
                Peer = $matches[1]
                Host = [int]$matches[2]
                App = [int]$matches[3]
                Status = ""
                Text = ""
                Raw = $line
            })
        } elseif ($line -match "send_reject seq=(\d+) status=(\d+)") {
            $rows.Add([pscustomobject]@{
                Device = $DeviceName
                Event = "send_reject"
                Local = ""
                Seq = [int]$matches[1]
                Peer = ""
                Host = [int]$matches[2]
                App = ""
                Status = ""
                Text = ""
                Raw = $line
            })
        } elseif ($line -match "rx src=([0-9A-F]+) flags=(\d+) text=(\S+)") {
            $rows.Add([pscustomobject]@{
                Device = $DeviceName
                Event = "rx"
                Local = ""
                Seq = ""
                Peer = $matches[1]
                Host = ""
                App = [int]$matches[2]
                Status = ""
                Text = $matches[3]
                Raw = $line
            })
        }
    }
    return $rows
}

if ($MinDelayMs -lt 0 -or $MaxDelayMs -lt $MinDelayMs) {
    throw "Invalid delay range: MinDelayMs=$MinDelayMs MaxDelayMs=$MaxDelayMs"
}

if (!(Get-Command adb -ErrorAction SilentlyContinue)) {
    throw "adb was not found in PATH"
}

if (!$OutputDir) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputDir = Join-Path "artifacts" "glimmer_chat_bidir_$stamp"
}
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$pixelTarget = Convert-ShortId $PixelTargetShortId
$huaweiTarget = Convert-ShortId $HuaweiTargetShortId
$runId = Get-Date -Format "HHmmss"
$random = [System.Random]::new()

Write-Host "OutputDir=$OutputDir"
Write-Host "Pixel=$PixelDevice target=$PixelTargetShortId/$pixelTarget"
Write-Host "Huawei=$HuaweiDevice target=$HuaweiTargetShortId/$huaweiTarget"

if ($Install) {
    if (!(Test-Path $ApkPath)) {
        throw "APK not found: $ApkPath"
    }
    Invoke-Adb -Device $PixelDevice -Args @("install", "-r", $ApkPath) | Out-Host
    Invoke-Adb -Device $HuaweiDevice -Args @("install", "-r", $ApkPath) | Out-Host
}

Invoke-Adb -Device $PixelDevice -Args @("logcat", "-c") | Out-Null
Invoke-Adb -Device $HuaweiDevice -Args @("logcat", "-c") | Out-Null

$pixelLogPath = Join-Path $OutputDir "pixel.log"
$huaweiLogPath = Join-Path $OutputDir "huawei.log"
$pixelLogErrPath = Join-Path $OutputDir "pixel.log.err"
$huaweiLogErrPath = Join-Path $OutputDir "huawei.log.err"
$pixelLogProcess = Start-GlimmerLogCapture -Device $PixelDevice -StdoutPath $pixelLogPath -StderrPath $pixelLogErrPath
$huaweiLogProcess = Start-GlimmerLogCapture -Device $HuaweiDevice -StdoutPath $huaweiLogPath -StderrPath $huaweiLogErrPath
Start-Sleep -Milliseconds 800

if (!$NoLaunch) {
    Start-GlimmerApp -Device $PixelDevice
    Start-GlimmerApp -Device $HuaweiDevice
    Start-Sleep -Milliseconds 2500
}

$schedule = New-Object System.Collections.Generic.List[object]
for ($i = 1; $i -le $CountPerSide; $i++) {
    $p2h = "P2H_T{0:D2}_{1}" -f $i, $runId
    Send-DebugChat -Device $PixelDevice -TargetShortId $pixelTarget -Text $p2h
    $schedule.Add([pscustomobject]@{ Direction = "P2H"; Index = $i; Text = $p2h })
    Start-Sleep -Milliseconds ($random.Next($MinDelayMs, $MaxDelayMs + 1))

    $h2p = "H2P_T{0:D2}_{1}" -f $i, $runId
    Send-DebugChat -Device $HuaweiDevice -TargetShortId $huaweiTarget -Text $h2p
    $schedule.Add([pscustomobject]@{ Direction = "H2P"; Index = $i; Text = $h2p })
    Start-Sleep -Milliseconds ($random.Next($MinDelayMs, $MaxDelayMs + 1))
}

$schedulePath = Join-Path $OutputDir "schedule.csv"
$schedule | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $schedulePath

Write-Host "Waiting $FinalWaitMs ms for TX results..."
Start-Sleep -Milliseconds $FinalWaitMs

Stop-GlimmerLogCapture -Process $pixelLogProcess
Stop-GlimmerLogCapture -Process $huaweiLogProcess
Start-Sleep -Milliseconds 500

$pixelLog = if ((Test-Path $pixelLogPath) -and (Get-Item $pixelLogPath).Length -gt 0) {
    @(Get-Content $pixelLogPath)
} else {
    $dump = Read-GlimmerLog -Device $PixelDevice
    $dump | Set-Content -Encoding UTF8 -Path $pixelLogPath
    @($dump)
}
$huaweiLog = if ((Test-Path $huaweiLogPath) -and (Get-Item $huaweiLogPath).Length -gt 0) {
    @(Get-Content $huaweiLogPath)
} else {
    $dump = Read-GlimmerLog -Device $HuaweiDevice
    $dump | Set-Content -Encoding UTF8 -Path $huaweiLogPath
    @($dump)
}

$rows = New-Object System.Collections.Generic.List[object]
Parse-GlimmerChatLog -DeviceName "Pixel" -Lines @($pixelLog) | ForEach-Object { $rows.Add($_) }
Parse-GlimmerChatLog -DeviceName "Huawei" -Lines @($huaweiLog) | ForEach-Object { $rows.Add($_) }

$eventsPath = Join-Path $OutputDir "events.csv"
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $eventsPath

$finishRows = $rows | Where-Object { $_.Event -eq "finish" }
$txResultRows = $rows | Where-Object { $_.Event -eq "tx_result" }
$rxRows = $rows | Where-Object { $_.Event -eq "rx" }
$statusGroups = @($finishRows | Group-Object Status | ForEach-Object { "$($_.Name)=$($_.Count)" })
$summary = [pscustomobject]@{
    OutputDir = $OutputDir
    Scheduled = $schedule.Count
    Enqueued = @($rows | Where-Object { $_.Event -eq "enqueue" }).Count
    Dispatched = @($rows | Where-Object { $_.Event -eq "dispatch" }).Count
    Finished = @($finishRows).Count
    TxResultOk = @($txResultRows | Where-Object { $_.Host -eq 0 -and $_.App -eq 0 }).Count
    TxResultNonOk = @($txResultRows | Where-Object { !($_.Host -eq 0 -and $_.App -eq 0) }).Count
    Rejected = @($rows | Where-Object { $_.Event -eq "send_reject" }).Count
    Received = @($rxRows).Count
    FinishStatus = ($statusGroups -join ";")
}

$summaryPath = Join-Path $OutputDir "summary.json"
$summary | ConvertTo-Json | Set-Content -Encoding UTF8 -Path $summaryPath
$summary | Format-List | Out-Host
Write-Host "Saved: $schedulePath"
Write-Host "Saved: $eventsPath"
Write-Host "Saved: $summaryPath"
