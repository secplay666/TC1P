param(
    [ValidateSet("1", "2", "3", "4", "BOARD1", "BOARD2", "BOARD3", "BOARD4", "COM9", "COM10", "COM16", "COM17", "A", "B", "ON", "OFF")]
    [string]$Target = "1",
    [string]$RelayPort = "COM18",
    [int]$BaudRate = 9600,
    [int]$SettleMs = 700
)

$ErrorActionPreference = "Stop"

function Get-RelayCommand {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Channel,
        [Parameter(Mandatory = $true)]
        [int]$State
    )

    $checksum = (0xA0 + $Channel + $State) -band 0xff
    return [byte[]](0xA0, $Channel, $State, $checksum)
}

function Write-RelayState {
    param(
        [Parameter(Mandatory = $true)]
        [System.IO.Ports.SerialPort]$Serial,
        [Parameter(Mandatory = $true)]
        [int]$Channel,
        [Parameter(Mandatory = $true)]
        [int]$State
    )

    $command = Get-RelayCommand -Channel $Channel -State $State
    $Serial.Write($command, 0, $command.Length)
    Start-Sleep -Milliseconds 160
}

$normalized = $Target.ToUpperInvariant()
switch ($normalized) {
    { $_ -in @("COM10") } {
        $targetName = "COM10"
        $states = @(0, 1, 0)
        break
    }
    { $_ -in @("COM9") } {
        $targetName = "COM9"
        $states = @(0, 0, 1)
        break
    }
    { $_ -in @("COM17") } {
        $targetName = "COM17"
        $states = @(0, 1, 1)
        break
    }
    { $_ -in @("COM16") } {
        $targetName = "COM16"
        $states = @(1, 1, 1)
        break
    }
    { $_ -in @("1", "BOARD1", "B", "OFF") } {
        $targetName = "Board1"
        $states = @(0, 0, 0)
        break
    }
    { $_ -in @("2", "BOARD2", "A", "ON") } {
        $targetName = "Board2"
        $states = @(1, 0, 0)
        break
    }
    { $_ -in @("3", "BOARD3") } {
        $targetName = "Board3"
        $states = @(0, 1, 0)
        break
    }
    { $_ -in @("4", "BOARD4") } {
        $targetName = "Board4"
        $states = @(0, 0, 1)
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
    Write-RelayState -Serial $serial -Channel 1 -State $states[0]
    Write-RelayState -Serial $serial -Channel 2 -State $states[1]
    Write-RelayState -Serial $serial -Channel 3 -State $states[2]
    $reply = $serial.ReadExisting()
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
}

Write-Host "Relay select: Target=$targetName State=$($states -join '') Port=$RelayPort Baud=$BaudRate Reply='$reply'"

if ($SettleMs -gt 0) {
    Start-Sleep -Milliseconds $SettleMs
}
