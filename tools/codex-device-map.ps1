[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-CodexRepoRoot {
    [CmdletBinding()]
    param(
        [string]$StartPath = $PSScriptRoot
    )

    $resolved = (Resolve-Path $StartPath).Path
    $current = $resolved

    while ($true) {
        if (Test-Path (Join-Path $current ".git")) {
            return $current
        }

        $parent = Split-Path -Parent $current
        if (-not $parent -or $parent -eq $current) {
            break
        }

        $current = $parent
    }

    throw "Repository root not found. Run this from inside a Git worktree."
}

function Get-CodexDeviceMapPath {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string]$DeviceMapPath
    )

    if ($DeviceMapPath) {
        if ([System.IO.Path]::IsPathRooted($DeviceMapPath)) {
            return $DeviceMapPath
        }

        return Join-Path $RepoRoot $DeviceMapPath
    }

    if ($env:CODEX_DEVICE_MAP_PATH) {
        if ([System.IO.Path]::IsPathRooted($env:CODEX_DEVICE_MAP_PATH)) {
            return $env:CODEX_DEVICE_MAP_PATH
        }

        return Join-Path $RepoRoot $env:CODEX_DEVICE_MAP_PATH
    }

    return Join-Path $env:USERPROFILE ".watche-robot\device-map.toml"
}

function Read-CodexDeviceMap {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path $Path)) {
        throw "Device map file not found: $Path. Create it first or copy tools\flashing\device-map.example.toml and fill in local ports."
    }

    $devices = @{}
    $currentAlias = $null
    $lineNumber = 0

    foreach ($rawLine in [System.IO.File]::ReadLines($Path)) {
        $lineNumber += 1
        $line = $rawLine.Trim()

        if (-not $line -or $line.StartsWith("#")) {
            continue
        }

        if ($line -match '^\[(?<section>[^\]]+)\]$') {
            $section = $Matches.section
            if ($section -match '^devices\.(?<alias>[A-Za-z0-9._-]+)$') {
                $currentAlias = $Matches.alias
                if (-not $devices.ContainsKey($currentAlias)) {
                    $devices[$currentAlias] = @{}
                }
                continue
            }

            $currentAlias = $null
            continue
        }

        if (-not $currentAlias) {
            continue
        }

        if ($line -notmatch '^(?<key>[A-Za-z0-9_-]+)\s*=\s*"(?<value>[^"]*)"\s*$') {
            throw "Device map parse error: ${Path}:$lineNumber -> $rawLine"
        }

        $devices[$currentAlias][$Matches.key] = $Matches.value
    }

    return $devices
}

function Resolve-CodexDeviceMapping {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Alias,

        [Parameter(Mandatory = $true)]
        [string]$RepoRoot,

        [string]$Firmware,

        [string]$DeviceMapPath
    )

    $mapPath = Get-CodexDeviceMapPath -RepoRoot $RepoRoot -DeviceMapPath $DeviceMapPath
    $resolvedMapPath = $mapPath
    if (Test-Path $mapPath) {
        $resolvedMapPath = (Resolve-Path $mapPath).Path
    }

    $devices = Read-CodexDeviceMap -Path $resolvedMapPath

    if (-not $devices.ContainsKey($Alias)) {
        throw "Alias '$Alias' not found in device map. Add [devices.$Alias] in $resolvedMapPath."
    }

    $entry = $devices[$Alias]
    $mappedFirmware = [string]$entry["firmware"]
    $mappedPort = [string]$entry["port"]

    if (-not $mappedPort) {
        throw "Device alias '$Alias' is missing the port field in $resolvedMapPath."
    }

    if ($Firmware -and $mappedFirmware -and $mappedFirmware -ne $Firmware) {
        throw "Device alias '$Alias' has firmware=$mappedFirmware, which does not match current firmware=$Firmware."
    }

    return [pscustomobject]@{
        Alias       = $Alias
        Firmware    = $mappedFirmware
        Port        = $mappedPort
        SourcePath  = $resolvedMapPath
    }
}
