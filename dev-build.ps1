#requires -Version 7
<#
.SYNOPSIS
  Local build + deploy helper for the PottyMuter fork (Windows).

.DESCRIPTION
  Builds the plugin with the same scripts CI uses and deploys the result
  into your OBS Studio installation, so the edit-build-test loop is:

      edit code -> .\dev-build.ps1 -> restart OBS -> test

.EXAMPLE
  .\dev-build.ps1                          # generic CPU, debug-friendly, deploy
  .\dev-build.ps1 -Acceleration nvidia     # CUDA variant
  .\dev-build.ps1 -NoBuild                 # deploy the previous build only
  .\dev-build.ps1 -NoDeploy                # build, but don't touch the OBS dir
  .\dev-build.ps1 -ObsDir "D:\OBS"         # non-default OBS installation

.NOTES
  * First run downloads ~2 GB of dependencies into .deps\ (one-time, cached).
  * Default configuration is RelWithDebInfo: optimized, but with debug info,
    so you can attach Visual Studio to obs64.exe and use breakpoints.
  * Deploying overwrites the ggml*/whisper/onnxruntime dlls in the OBS folder
    with the variant you build. If you also keep the upstream CleanStream
    plugin (obs-cleanstream.dll) installed and rely on its GPU features,
    build with the same acceleration you had there (-Acceleration nvidia).
#>
[CmdletBinding()]
param(
    [ValidateSet('generic', 'nvidia', 'amd')]
    [string] $Acceleration = 'generic',

    [ValidateSet('RelWithDebInfo', 'Debug', 'Release', 'MinSizeRel')]
    [string] $Config = 'RelWithDebInfo',

    [string] $ObsDir = 'C:\Program Files\obs-studio',

    # Skip the build step, only deploy (retry a previous build)
    [switch] $NoBuild,

    # Build but do not copy anything into the OBS installation
    [switch] $NoDeploy,

    # Do not run the winget dependency install step inside Build-Windows.ps1
    [switch] $NoDeps
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = $PSScriptRoot
$ReleaseDir = Join-Path $ProjectRoot "release\$Config"

if ( -not $NoBuild ) {
    # NOTE: hashtable splat (named parameters). Array splat would pass its
    # elements as positional values, breaking -Configuration.
    $buildArgs = @{ Target = 'x64'; Configuration = $Config }
    if ( $NoDeps ) { $buildArgs.SkipDeps = $true }
    $env:ACCELERATION = $Acceleration
    & (Join-Path $ProjectRoot '.github/scripts/Build-Windows.ps1') @buildArgs
    if ( $LASTEXITCODE -ne 0 ) { throw 'Build failed.' }
}

if ( $NoDeploy ) {
    Write-Host "`nBuild finished. Artifacts (not deployed):" -ForegroundColor Green
    Write-Host "  $ReleaseDir"
    return
}

# --- Deploy into OBS -------------------------------------------------------
$PluginDll = Join-Path $ReleaseDir 'obs-plugins\64bit\obs-pottymuter.dll'
if ( -not (Test-Path $PluginDll) ) {
    throw "Plugin dll not found at '$PluginDll' - did the build + install step run?"
}

if ( -not (Test-Path (Join-Path $ObsDir 'obs-plugins\64bit')) ) {
    throw "OBS plugin directory not found under '$ObsDir'. Pass the right one with -ObsDir."
}

Write-Host "`nDeploying to $ObsDir ..." -ForegroundColor Cyan

# Same layout as CI produces: obs-plugins\64bit\*.dll(+pdb), data\,
# plus whisper/ggml/onnxruntime runtime dlls. Mirrors the manual step in
# README ("Copy-Item release\<config>\* -> C:\Program Files\obs-studio\").
# NOTE: this overwrites the ggml*/whisper/onnxruntime dlls in the OBS
# folder with the variant being built - see .NOTES above.

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
    ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

$RoboArgs = @("/E", "/NJH", "/NJS", "/NP", "/NFL", "/NDL", "/R:2", "/W:2")

if ( $isAdmin ) {
    robocopy $ReleaseDir $ObsDir @RoboArgs
    if ( $LASTEXITCODE -ge 8 ) { throw "Deploy failed (robocopy exit $LASTEXITCODE)." }
} else {
    # Program Files (or any admin-only target): escalate just the copy step
    Write-Host "No admin rights - elevating the copy step (confirm the UAC prompt)." -ForegroundColor Yellow
    $copyScript = Join-Path $env:TEMP "pottymuter-elevated-deploy.ps1"
    $inner = "robocopy '$ReleaseDir' '$ObsDir' $($RoboArgs -join ' '); if (`$LASTEXITCODE -ge 8) { throw 'robocopy failed' } else { exit 0 }"
    Set-Content -Path $copyScript -Value $inner -Encoding UTF8
    $proc = Start-Process pwsh -Verb RunAs -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$copyScript`"") -Wait -PassThru
    if ( $proc.ExitCode -ne 0 ) { throw "Elevated deploy failed (exit $($proc.ExitCode))." }
}

Write-Host @"
Done. Deployed variant: $Acceleration ($Config)

Next steps:
  1. restart OBS (add or refresh the 'PottyMuter audio filter' on a source)
  2. to debug: open the generated PDB with Visual Studio -> Debug ->
     Attach to Process... -> obs64.exe
"@ -ForegroundColor Green
