[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [string]$BuildDirectoryName = "build-resource-regression"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$firmwareRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent (Split-Path -Parent $firmwareRoot)
$suites = @(
    @{
        Name = "main"
        Source = Join-Path $firmwareRoot "main\test_support\host"
    },
    @{
        Name = "behavior_state_service"
        Source = Join-Path $firmwareRoot "components\services\behavior_state_service\test_support\host"
    },
    @{
        Name = "anim_service"
        Source = Join-Path $firmwareRoot "components\services\anim_service\test_support\host"
    },
    @{
        Name = "hal_display"
        Source = Join-Path $firmwareRoot "components\hal\hal_display\test_support\host"
    },
    @{
        Name = "sfx_service"
        Source = Join-Path $firmwareRoot "components\services\sfx_service\test_support\host"
    },
    @{
        Name = "mcu_motion_service"
        Source = Join-Path $firmwareRoot "components\services\mcu_motion_service\test_support\host"
    },
    @{
        Name = "provision_manager"
        Source = Join-Path $firmwareRoot "components\services\provision_manager\test_support\host"
    }
)

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Executable,

        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE"
    }
}

foreach ($suite in $suites) {
    $buildPath = Join-Path $suite.Source $BuildDirectoryName
    Write-Host "== Resource host suite: $($suite.Name) =="
    Invoke-CheckedCommand -Executable "cmake" -Arguments @("-S", $suite.Source, "-B", $buildPath)
    Invoke-CheckedCommand -Executable "cmake" -Arguments @("--build", $buildPath, "--config", $Configuration)
    Invoke-CheckedCommand -Executable "ctest" -Arguments @(
        "--test-dir", $buildPath,
        "-C", $Configuration,
        "--output-on-failure"
    )
}

Write-Host "== Resource budget unit tests =="
Invoke-CheckedCommand -Executable "python" -Arguments @(
    (Join-Path $PSScriptRoot "test_resource_budget.py")
)

Write-Host "== Animation registry generated artifact check =="
Invoke-CheckedCommand -Executable "python" -Arguments @(
    (Join-Path $firmwareRoot "tools\generate_animation_registry.py"),
    "--check"
)

Write-Host "== Animation registry and asset pipeline tests =="
Invoke-CheckedCommand -Executable "python" -Arguments @(
    "-m",
    "pytest",
    (Join-Path $repoRoot "tools\tests\test_animation_registry_codegen.py"),
    (Join-Path $repoRoot "tools\tests\test_sync_animation_metadata.py"),
    (Join-Path $repoRoot "tools\tests\test_generate_anim_assets.py"),
    (Join-Path $repoRoot "tools\tests\test_sync_anim_sdcard.py"),
    "-q"
)

Write-Host "All resource host suites passed."
