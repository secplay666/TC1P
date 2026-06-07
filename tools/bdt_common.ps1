function Get-BdtDeviceList {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Bdt
    )

    $listOutput = (& $Bdt all 2>&1) | Out-String
    if ($LASTEXITCODE -ne 0) {
        throw "BDT list devices failed with exit code $LASTEXITCODE"
    }

    $devices = @()
    $current = $null
    foreach ($line in ($listOutput -split "`r?`n")) {
        if ($line -match "Device ID:\s*(\d+)\s*<\s*(.*?)\s*>") {
            $current = [ordered]@{
                DeviceId   = [int]$Matches[1]
                DeviceInfo = $Matches[2]
                Vid        = ""
                Pid        = ""
                PortNum    = $null
                HubNum     = $null
            }
            if ($current.DeviceInfo -match "vid_([0-9a-fA-F]+)&pid_([0-9a-fA-F]+)") {
                $current.Vid = $Matches[1].ToLowerInvariant()
                $current.Pid = $Matches[2].ToLowerInvariant()
            }
            continue
        }

        if ($null -ne $current -and $line -match "PortNum:(\d+),\s*HubNum:(\d+)") {
            $current.PortNum = [int]$Matches[1]
            $current.HubNum = [int]$Matches[2]
            $devices += [pscustomobject]$current
            $current = $null
        }
    }

    return [pscustomobject]@{
        Raw     = $listOutput
        Devices = $devices
    }
}

function Get-BdtDeviceFingerprint {
    param(
        [Parameter(Mandatory = $true)]
        $DeviceList
    )

    return (($DeviceList.Devices | Sort-Object DeviceId | ForEach-Object {
                "$($_.DeviceId):$($_.Vid):$($_.Pid):$($_.PortNum):$($_.HubNum)"
            }) -join "|")
}

function Get-StableBdtDeviceList {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Bdt
    )

    $first = Get-BdtDeviceList -Bdt $Bdt
    Start-Sleep -Milliseconds 200
    $second = Get-BdtDeviceList -Bdt $Bdt

    $firstFingerprint = Get-BdtDeviceFingerprint -DeviceList $first
    $secondFingerprint = Get-BdtDeviceFingerprint -DeviceList $second
    if ($firstFingerprint -ne $secondFingerprint) {
        throw "BDT device list is not stable between two reads.`nFirst:`n$($first.Raw)`nSecond:`n$($second.Raw)"
    }

    return $second
}

function Resolve-BdtEvkPortNum {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Bdt,
        [int]$BoardId = 1,
        [string]$DebuggerPid = "",
        [int]$PortNum = -1,
        [int]$HubNum = -1
    )

    $DebuggerPid = $DebuggerPid.Trim().ToLowerInvariant()
    if ($DebuggerPid.StartsWith("0x")) {
        $DebuggerPid = $DebuggerPid.Substring(2)
    }
    if ($DebuggerPid.StartsWith("pid_")) {
        $DebuggerPid = $DebuggerPid.Substring(4)
    }

    $list = Get-StableBdtDeviceList -Bdt $Bdt

    if ($PortNum -ge 0) {
        if ($HubNum -lt 0) {
            throw "-HubNum is required when -PortNum is specified."
        }
        $matches = @($list.Devices | Where-Object {
                $_.PortNum -eq $PortNum -and $_.HubNum -eq $HubNum
            })
        if ($matches.Count -ne 1) {
            $found = ($list.Devices | ForEach-Object {
                    "DeviceId=$($_.DeviceId) pid=$($_.Pid) PortNum=$($_.PortNum) HubNum=$($_.HubNum)"
                }) -join "`n"
            throw "Expected exactly one BDT device at PortNum=$PortNum HubNum=$HubNum, found $($matches.Count).`n$found`nRaw BDT output:`n$($list.Raw)"
        }

        return [pscustomobject]@{
            BoardId     = $BoardId
            DebuggerPid = $matches[0].Pid
            PortNum     = $matches[0].PortNum
            HubNum      = $matches[0].HubNum
            BdtDeviceId = $matches[0].DeviceId
            Source      = "PortHub"
            List        = $list
        }
    }

    if (![string]::IsNullOrWhiteSpace($DebuggerPid)) {
        $matches = @($list.Devices | Where-Object {
                $_.Vid -eq "248a" -and $_.Pid -eq $DebuggerPid -and $null -ne $_.PortNum
            })
        if ($matches.Count -ne 1) {
            $found = ($list.Devices | ForEach-Object {
                    "DeviceId=$($_.DeviceId) pid=$($_.Pid) PortNum=$($_.PortNum) HubNum=$($_.HubNum)"
                }) -join "`n"
            throw "Expected exactly one Telink debugger pid_$DebuggerPid, found $($matches.Count).`n$found`nRaw BDT output:`n$($list.Raw)"
        }

        return [pscustomobject]@{
            BoardId     = $BoardId
            DebuggerPid = $DebuggerPid
            PortNum     = $matches[0].PortNum
            HubNum      = $matches[0].HubNum
            BdtDeviceId = $matches[0].DeviceId
            Source      = "DebuggerPid"
            List        = $list
        }
    }

    $ordinalMatch = @($list.Devices | Where-Object { $_.DeviceId -eq $BoardId -and $null -ne $_.PortNum })
    if ($ordinalMatch.Count -ne 1) {
        throw "BDT DeviceId=$BoardId not found or has no PortNum.`n$($list.Raw)"
    }
    if ($ordinalMatch[0].Pid -eq "8801") {
        $evkMatches = @($list.Devices | Where-Object {
                $_.Vid -eq "248a" -and $_.Pid -ne "8801" -and $null -ne $_.PortNum
            })
        if ($evkMatches.Count -eq 1) {
            return [pscustomobject]@{
                BoardId     = $BoardId
                DebuggerPid = $evkMatches[0].Pid
                PortNum     = $evkMatches[0].PortNum
                HubNum      = $evkMatches[0].HubNum
                BdtDeviceId = $evkMatches[0].DeviceId
                Source      = "AutoEvkFallback"
                List        = $list
            }
        }
        $found = ($list.Devices | ForEach-Object {
                "DeviceId=$($_.DeviceId) pid=$($_.Pid) PortNum=$($_.PortNum) HubNum=$($_.HubNum)"
            }) -join "`n"
        throw "BDT DeviceId=$BoardId is pid_8801 USB download interface, and EVK auto fallback found $($evkMatches.Count) candidates.`n$found`nRaw BDT output:`n$($list.Raw)"
    }

    return [pscustomobject]@{
        BoardId     = $BoardId
        DebuggerPid = $ordinalMatch[0].Pid
        PortNum     = $ordinalMatch[0].PortNum
        HubNum      = $ordinalMatch[0].HubNum
        BdtDeviceId = $ordinalMatch[0].DeviceId
        Source      = "BdtOrdinal"
        List        = $list
    }
}
