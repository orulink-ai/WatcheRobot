[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Port,

    [Parameter()]
    [string]$DeviceAlias,

    [Parameter()]
    [string]$DeviceMapPath,

    [Parameter()]
    [string]$ProjectPath,

    [Parameter()]
    [string]$BuildPath,

    [Parameter()]
    [switch]$NoBuild,

    [Parameter()]
    [switch]$NoWake,

    [Parameter()]
    [switch]$WakeWord,

    [Parameter()]
    [switch]$NoMonitor,

    [Parameter()]
    [int]$MonitorSeconds,

    [Parameter()]
    [int]$MonitorMaxLines,

    [Parameter()]
    [string]$MonitorLogPath,

    [Parameter()]
    [switch]$DryRun,

    [Parameter()]
    [switch]$AutoSelectHighestPort,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ExtraIdfArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$codexDeviceMapHelper = Join-Path $PSScriptRoot "..\..\..\tools\codex-device-map.ps1"
if (Test-Path $codexDeviceMapHelper) {
    . $codexDeviceMapHelper
}

if (-not $ProjectPath) {
    if (-not $PSScriptRoot) {
        throw "Cannot resolve script directory. Pass -ProjectPath explicitly."
    }
    $ProjectPath = Split-Path -Parent $PSScriptRoot
}

function Resolve-ProjectDescriptionPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResolvedProjectPath,

        [string]$ResolvedBuildPath
    )

    if ($ResolvedBuildPath) {
        $candidate = Join-Path $ResolvedBuildPath "project_description.json"
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $defaultCandidate = Join-Path $ResolvedProjectPath "build\project_description.json"
    if (Test-Path $defaultCandidate) {
        return $defaultCandidate
    }

    return $null
}

function Resolve-IdfPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResolvedProjectPath,

        [string]$ResolvedBuildPath
    )

    if ($env:IDF_PATH -and (Test-Path $env:IDF_PATH)) {
        return $env:IDF_PATH
    }

    $projectDescriptionPath = Resolve-ProjectDescriptionPath -ResolvedProjectPath $ResolvedProjectPath -ResolvedBuildPath $ResolvedBuildPath
    if ($projectDescriptionPath) {
        $projectDescription = Get-Content $projectDescriptionPath -Raw | ConvertFrom-Json
        if ($projectDescription.idf_path -and (Test-Path $projectDescription.idf_path)) {
            return $projectDescription.idf_path
        }
    }

    $fallbacks = @(
        "C:\Espressif\frameworks\esp-idf-v5.2.1",
        "C:\Espressif\frameworks\esp-idf"
    )

    foreach ($candidate in $fallbacks) {
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    throw "ESP-IDF not found. Set IDF_PATH or ensure build/project_description.json has a valid idf_path."
}

function Resolve-IdfPythonEnvPath {
    if ($env:IDF_PYTHON_ENV_PATH -and (Test-Path (Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"))) {
        return $env:IDF_PYTHON_ENV_PATH
    }

    $preferred = "C:\Espressif\python_env\idf5.2_py3.11_env"
    if (Test-Path (Join-Path $preferred "Scripts\python.exe")) {
        return $preferred
    }

    $pythonEnvRoot = "C:\Espressif\python_env"
    if (Test-Path $pythonEnvRoot) {
        $candidate = Get-ChildItem -Path $pythonEnvRoot -Directory -Filter "idf5.2_py*_env" -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName "Scripts\python.exe") } |
            Sort-Object Name |
            Select-Object -First 1
        if ($candidate) {
            return $candidate.FullName
        }
    }

    return $null
}

function Resolve-IdfBootstrapScript {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResolvedIdfPath
    )

    $exportScript = Join-Path $ResolvedIdfPath "export.ps1"
    if (Test-Path $exportScript) {
        return $exportScript
    }

    $installRoot = Split-Path -Parent (Split-Path -Parent $ResolvedIdfPath)
    $initializeScript = Join-Path $installRoot "Initialize-Idf.ps1"
    if (Test-Path $initializeScript) {
        return $initializeScript
    }

    throw "ESP-IDF bootstrap script not found. Checked: $initializeScript, $exportScript"
}

function Resolve-FlashPort {
    param(
        [string]$RequestedPort,

        [string]$RequestedDeviceAlias,

        [string]$RepoRoot,

        [string]$FirmwareName,

        [string]$ResolvedDeviceMapPath,

        [switch]$AllowHighestPortAutoSelect
    )

    if ($RequestedDeviceAlias) {
        if (-not (Get-Command Resolve-CodexDeviceMapping -ErrorAction SilentlyContinue)) {
            throw "Device map resolver not found. Ensure tools\codex-device-map.ps1 exists."
        }

        $mapping = Resolve-CodexDeviceMapping -Alias $RequestedDeviceAlias -RepoRoot $RepoRoot -Firmware $FirmwareName -DeviceMapPath $ResolvedDeviceMapPath
        return $mapping.Port
    }

    if ($RequestedPort) {
        if ($RequestedPort -match '^COM\d+$') {
            return $RequestedPort.ToUpperInvariant()
        }

        return $RequestedPort
    }

    $ports = [System.IO.Ports.SerialPort]::GetPortNames() |
        Where-Object { $_ -match '^COM\d+$' } |
        Sort-Object { [int]($_ -replace '^COM', '') }

    if (-not $ports -or $ports.Count -eq 0) {
        throw "No serial ports detected. Cannot auto-select a flash port."
    }

    if ($ports.Count -eq 1) {
        return $ports[0].ToUpperInvariant()
    }

    if ($AllowHighestPortAutoSelect) {
        return $ports[-1].ToUpperInvariant()
    }

    throw "Multiple serial ports detected: $($ports -join ', '). Pass -Port COMx explicitly or use -DeviceAlias / tools\run-lane.ps1."
}

function Resolve-OutputPath {
    param(
        [string]$RequestedPath,

        [Parameter(Mandatory = $true)]
        [string]$BaseDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DefaultLeafName
    )

    if (-not $RequestedPath) {
        return Join-Path $BaseDirectory $DefaultLeafName
    }

    if ([System.IO.Path]::IsPathRooted($RequestedPath)) {
        return [System.IO.Path]::GetFullPath($RequestedPath)
    }

    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $RequestedPath))
}

function Resolve-ProjectVersion {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResolvedProjectPath
    )

    $cmakeListsPath = Join-Path $ResolvedProjectPath "CMakeLists.txt"
    if (-not (Test-Path $cmakeListsPath)) {
        return $null
    }

    $cmakeLists = Get-Content $cmakeListsPath -Raw
    $match = [regex]::Match($cmakeLists, 'set\s*\(\s*PROJECT_VER\s+"([^"]+)"\s*\)')
    if ($match.Success) {
        return $match.Groups[1].Value
    }

    return $null
}

function Stop-ProcessTree {
    param(
        [Parameter(Mandatory = $true)]
        [int]$RootProcessId
    )

    $processTable = @{}
    foreach ($process in Get-CimInstance Win32_Process -ErrorAction SilentlyContinue) {
        $parentKey = [string][int]$process.ParentProcessId
        if (-not $processTable.ContainsKey($parentKey)) {
            $processTable[$parentKey] = New-Object System.Collections.Generic.List[int]
        }
        $processTable[$parentKey].Add([int]$process.ProcessId)
    }

    $toStop = New-Object System.Collections.Generic.List[int]
    $stack = New-Object System.Collections.Generic.Stack[int]
    $stack.Push($RootProcessId)

    while ($stack.Count -gt 0) {
        $current = $stack.Pop()
        if ($toStop.Contains($current)) {
            continue
        }

        $toStop.Add($current)
        $childKey = [string]$current
        if ($processTable.ContainsKey($childKey)) {
            foreach ($childId in $processTable[$childKey]) {
                $stack.Push($childId)
            }
        }
    }

    foreach ($processId in ($toStop | Sort-Object -Descending)) {
        try {
            Stop-Process -Id $processId -Force -ErrorAction Stop
        }
        catch {
        }
    }
}

function Invoke-BoundedMonitor {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ResolvedProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$ResolvedPort,

        [string]$ResolvedBuildPath,

        [Parameter(Mandatory = $true)]
        [string]$IdfBootstrapScript,

        [Parameter(Mandatory = $true)]
        [string]$ResolvedIdfPath,

        [Parameter(Mandatory = $true)]
        [string[]]$MonitorIdfArgs,

        [int]$LineLimit,

        [int]$TimeLimitSeconds,

        [string]$RequestedLogPath
    )

    $logDirectory = if ($ResolvedBuildPath) { $ResolvedBuildPath } else { $ResolvedProjectPath }
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $stdoutLog = Resolve-OutputPath -RequestedPath $RequestedLogPath -BaseDirectory $logDirectory -DefaultLeafName "monitor-$($ResolvedPort.ToLowerInvariant())-$timestamp.log"
    $stderrLog = if ($stdoutLog.EndsWith(".log")) {
        $stdoutLog.Substring(0, $stdoutLog.Length - 4) + ".err.log"
    } else {
        "$stdoutLog.err.log"
    }

    $stdoutDir = Split-Path -Parent $stdoutLog
    $stderrDir = Split-Path -Parent $stderrLog
    if ($stdoutDir) {
        $null = New-Item -ItemType Directory -Path $stdoutDir -Force
    }
    if ($stderrDir) {
        $null = New-Item -ItemType Directory -Path $stderrDir -Force
    }

    $quotedArgs = ($MonitorIdfArgs | ForEach-Object { "'" + ($_ -replace "'", "''") + "'" }) -join ", "
    $monitorRunner = Join-Path ([System.IO.Path]::GetTempPath()) "codex-idf-monitor-$([guid]::NewGuid().ToString('N')).ps1"
    $monitorScript = @"
`$ErrorActionPreference = 'Stop'
if (-not (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue)) {
    `$IsWindows = `$true
}
`$env:IDF_PATH = '$($ResolvedIdfPath -replace "'", "''")'
if ('$($script:idfPythonEnvPath -replace "'", "''")') {
    `$env:IDF_PYTHON_ENV_PATH = '$($script:idfPythonEnvPath -replace "'", "''")'
}
. '$($IdfBootstrapScript -replace "'", "''")' | Out-Null
if (-not (Get-Command 'idf.py' -ErrorAction SilentlyContinue)) {
    throw 'ESP-IDF environment loaded, but idf.py was not found.'
}
`$monitorArgs = @($quotedArgs)
& idf.py @monitorArgs
exit `$LASTEXITCODE
"@
    Set-Content -Path $monitorRunner -Value $monitorScript -Encoding UTF8

    $process = $null
    try {
        $null = New-Item -ItemType File -Path $stdoutLog -Force
        $null = New-Item -ItemType File -Path $stderrLog -Force
        $process = Start-Process -FilePath "powershell.exe" `
            -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $monitorRunner) `
            -WorkingDirectory $ResolvedProjectPath `
            -PassThru `
            -RedirectStandardOutput $stdoutLog `
            -RedirectStandardError $stderrLog
        $startedAt = Get-Date
        $stopReason = "process_exit"

        while (-not $process.HasExited) {
            Start-Sleep -Milliseconds 500

            if ($TimeLimitSeconds -gt 0 -and ((Get-Date) - $startedAt).TotalSeconds -ge $TimeLimitSeconds) {
                $stopReason = "time_limit"
                Stop-ProcessTree -RootProcessId $process.Id
                break
            }

            if ($LineLimit -gt 0 -and (Test-Path $stdoutLog)) {
                $currentLineCount = (Get-Content -Path $stdoutLog -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
                if ($currentLineCount -ge $LineLimit) {
                    $stopReason = "line_limit"
                    Stop-ProcessTree -RootProcessId $process.Id
                    break
                }
            }
        }

        if (-not $process.HasExited) {
            $process.WaitForExit()
        }

        $stdoutContent = if (Test-Path $stdoutLog) { Get-Content -Path $stdoutLog -Raw -ErrorAction SilentlyContinue } else { "" }
        $stderrContent = if (Test-Path $stderrLog) { Get-Content -Path $stderrLog -Raw -ErrorAction SilentlyContinue } else { "" }
        $lineCount = if (Test-Path $stdoutLog) {
            (Get-Content -Path $stdoutLog -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
        } else {
            0
        }

        Write-Host "Monitor : $stopReason"
        Write-Host "Log     : $stdoutLog"
        if ($stderrContent) {
            Write-Host "ErrLog  : $stderrLog"
        }

        $tailLineCount = if ($LineLimit -gt 0) { [Math]::Min($LineLimit, 40) } else { 40 }
        if ($tailLineCount -lt 1) {
            $tailLineCount = 20
        }

        if ($stdoutContent) {
            $tailLines = $stdoutContent -split "(`r`n|`n|`r)" | Where-Object { $_ -ne "" } | Select-Object -Last $tailLineCount
            if ($tailLines) {
                Write-Host "Monitor tail:"
                foreach ($line in $tailLines) {
                    Write-Host $line
                }
            }
        }

        if ($stopReason -eq "process_exit" -and $process.ExitCode -ne 0) {
            throw "monitor exited with non-zero code: $($process.ExitCode)"
        }

        return [pscustomobject]@{
            LogPath      = $stdoutLog
            ErrorLogPath = $stderrLog
            StopReason   = $stopReason
            LineCount    = $lineCount
            ExitCode     = $process.ExitCode
        }
    }
    finally {
        if (Test-Path $monitorRunner) {
            Remove-Item -Path $monitorRunner -Force -ErrorAction SilentlyContinue
        }
        if ($null -ne $process) {
            $process.Dispose()
        }
    }
}

$resolvedProjectPath = (Resolve-Path $ProjectPath).Path
$resolvedRepoRoot = Split-Path -Parent (Split-Path -Parent $resolvedProjectPath)
$firmwareName = Split-Path -Leaf $resolvedProjectPath
$projectVersion = Resolve-ProjectVersion -ResolvedProjectPath $resolvedProjectPath
$resolvedBuildPath = $null
if ($BuildPath) {
    if (Test-Path $BuildPath) {
        $resolvedBuildPath = (Resolve-Path $BuildPath).Path
    } elseif ([System.IO.Path]::IsPathRooted($BuildPath)) {
        $resolvedBuildPath = [System.IO.Path]::GetFullPath($BuildPath)
    } else {
        $resolvedBuildPath = [System.IO.Path]::GetFullPath((Join-Path $resolvedProjectPath $BuildPath))
    }
}

if ($NoWake -and $WakeWord) {
    throw "-NoWake and -WakeWord cannot be used together."
}

$useNoWake = $NoWake -or ((-not $WakeWord) -and $projectVersion -eq "V2.3.0")
$resolvedSdkconfigPath = $null
$sdkconfigDefaultsValue = $null
if ($useNoWake) {
    if (-not $BuildPath) {
        $resolvedBuildPath = [System.IO.Path]::GetFullPath((Join-Path $resolvedProjectPath "build-no-wake"))
    }

    $resolvedSdkconfigPath = [System.IO.Path]::GetFullPath((Join-Path $resolvedProjectPath "sdkconfig.no-wake"))
    $sdkconfigDefaultPaths = @(
        [System.IO.Path]::GetFullPath((Join-Path $resolvedProjectPath "sdkconfig.defaults")),
        [System.IO.Path]::GetFullPath((Join-Path $resolvedProjectPath "sdkconfig.no-wake.defaults"))
    )

    foreach ($sdkconfigDefaultPath in $sdkconfigDefaultPaths) {
        if (-not (Test-Path $sdkconfigDefaultPath)) {
            throw "no-wake build is missing default sdkconfig file: $sdkconfigDefaultPath"
        }
    }

    $sdkconfigDefaultsValue = $sdkconfigDefaultPaths -join ";"
}

$resolvedPort = Resolve-FlashPort -RequestedPort $Port `
    -RequestedDeviceAlias $DeviceAlias `
    -RepoRoot $resolvedRepoRoot `
    -FirmwareName $firmwareName `
    -ResolvedDeviceMapPath $DeviceMapPath `
    -AllowHighestPortAutoSelect:$AutoSelectHighestPort

$idfPath = Resolve-IdfPath -ResolvedProjectPath $resolvedProjectPath -ResolvedBuildPath $resolvedBuildPath
$idfBootstrapScript = Resolve-IdfBootstrapScript -ResolvedIdfPath $idfPath
$idfPythonEnvPath = Resolve-IdfPythonEnvPath

$flashArgs = @()
if ($resolvedBuildPath) {
    $flashArgs += "-B"
    $flashArgs += $resolvedBuildPath
}
if ($useNoWake) {
    $flashArgs += "-D"
    $flashArgs += "SDKCONFIG=$resolvedSdkconfigPath"
    $flashArgs += "-D"
    $flashArgs += "SDKCONFIG_DEFAULTS=$sdkconfigDefaultsValue"
}

$flashArgs += "-p"
$flashArgs += $resolvedPort

if (-not $NoBuild) {
    $flashArgs += "build"
}

$flashArgs += "flash"

$monitorArgs = @()
if ($resolvedBuildPath) {
    $monitorArgs += "-B"
    $monitorArgs += $resolvedBuildPath
}
$monitorArgs += "-p"
$monitorArgs += $resolvedPort
$monitorArgs += "monitor"

if ($ExtraIdfArgs) {
    $flashArgs += $ExtraIdfArgs
    $monitorArgs += $ExtraIdfArgs
}

Write-Host "Project : $resolvedProjectPath"
if ($projectVersion) {
    Write-Host "Version : $projectVersion"
}
if ($resolvedBuildPath) {
    Write-Host "Build   : $resolvedBuildPath"
}
Write-Host "Variant : $(if ($useNoWake) { 'no-wake' } else { 'wake-word' })"
if ($useNoWake) {
    Write-Host "SDKCONF : $resolvedSdkconfigPath"
    Write-Host "Defaults: $sdkconfigDefaultsValue"
}
if ($DeviceAlias) {
    Write-Host "Device  : $DeviceAlias"
}
Write-Host "IDF     : $idfPath"
if ($idfPythonEnvPath) {
    Write-Host "IDF Py  : $idfPythonEnvPath"
}
Write-Host "Port    : $resolvedPort"
Write-Host "Flash   : idf.py $($flashArgs -join ' ')"
if ($NoMonitor) {
    Write-Host "Monitor : disabled"
} elseif ($MonitorSeconds -gt 0 -or $MonitorMaxLines -gt 0) {
    Write-Host "Monitor : bounded"
    if ($MonitorSeconds -gt 0) {
        Write-Host "Seconds : $MonitorSeconds"
    }
    if ($MonitorMaxLines -gt 0) {
        Write-Host "Lines   : $MonitorMaxLines"
    }
    if ($MonitorLogPath) {
        Write-Host "LogPath : $MonitorLogPath"
    }
} else {
    Write-Host "Monitor : interactive"
    Write-Host "Command : idf.py $($monitorArgs -join ' ')"
}

if ($DryRun) {
    exit 0
}

Push-Location $resolvedProjectPath
try {
    # ESP-IDF's export.ps1 probes $IsWindows before defining it in some shells.
    if (-not (Get-Variable -Name IsWindows -ErrorAction SilentlyContinue)) {
        $IsWindows = $true
    }
    $env:IDF_PATH = $idfPath
    if ($idfPythonEnvPath) {
        $env:IDF_PYTHON_ENV_PATH = $idfPythonEnvPath
    }
    . $idfBootstrapScript | Out-Null
    if (-not (Get-Command "idf.py" -ErrorAction SilentlyContinue)) {
        throw "ESP-IDF environment loaded, but idf.py was not found."
    }

    & idf.py @flashArgs
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    if ($NoMonitor) {
        exit 0
    }

    if ($MonitorSeconds -gt 0 -or $MonitorMaxLines -gt 0) {
        $monitorResult = Invoke-BoundedMonitor -ResolvedProjectPath $resolvedProjectPath `
            -ResolvedPort $resolvedPort `
            -ResolvedBuildPath $resolvedBuildPath `
            -IdfBootstrapScript $idfBootstrapScript `
            -ResolvedIdfPath $idfPath `
            -MonitorIdfArgs $monitorArgs `
            -LineLimit $MonitorMaxLines `
            -TimeLimitSeconds $MonitorSeconds `
            -RequestedLogPath $MonitorLogPath

        if ($monitorResult.ExitCode -ne 0 -and $monitorResult.StopReason -eq "process_exit") {
            exit $monitorResult.ExitCode
        }

        exit 0
    }

    & idf.py @monitorArgs
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
