param(
    [ValidateSet("COM16", "COM17", "A", "B", "ON", "OFF")]
    [string]$Target = "COM16",
    [string]$RelayPort = "COM18",
    [int]$BaudRate = 9600,
    [int]$SettleMs = 700
)

$ErrorActionPreference = "Stop"

$normalized = $Target.ToUpperInvariant()
switch ($normalized) {
    { $_ -in @("COM16", "A", "ON") } {
        $relayName = "ON"
        $targetUart = "COM16"
        $command = [byte[]](0xA0, 0x01, 0x01, 0xA2)
        break
    }
    { $_ -in @("COM17", "B", "OFF") } {
        $relayName = "OFF"
        $targetUart = "COM17"
        $command = [byte[]](0xA0, 0x01, 0x00, 0xA1)
        break
    }
    default {
        throw "Unsupported relay target: $Target"
    }
}

$serial = New-Object System.IO.Ports.SerialPort $RelayPort, $BaudRate, "None", 8, "One"
$serial.ReadTimeout = 300
$serial.WriteTimeout = 1000

try {
    $serial.Open()
    Start-Sleep -Milliseconds 80
    $serial.DiscardInBuffer()
    $serial.DiscardOutBuffer()
    $serial.Write($command, 0, $command.Length)
    Start-Sleep -Milliseconds 250
    $reply = $serial.ReadExisting()
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}

Write-Host "Relay select: Target=$targetUart Relay=$relayName Port=$RelayPort Baud=$BaudRate Reply='$reply'"

if ($SettleMs -gt 0) {
    Start-Sleep -Milliseconds $SettleMs
}
