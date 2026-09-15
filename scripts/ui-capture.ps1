# Background UI capture. The executable isolates itself before QApplication is created.
# Example: pwsh scripts/ui-capture.ps1 -Scene resources/scenes/lantern.scene.json -Raster
[CmdletBinding()]
param(
    [string]$Scene = '',
    [ValidatePattern('^[\w.-]+$')][string]$Tag = 'default',
    [ValidateRange(320,8192)][int]$Width = 1600,
    [ValidateRange(320,8192)][int]$Height = 900,
    [string]$ScaleFactor = '',
    [string]$Exe = 'build\Release\learnQT.exe',
    [string]$OutDir = 'build\ui-capture',
    [ValidateRange(1,3600)][int]$StartupTimeoutSec = 180,
    [ValidateRange(0,60)][int]$SettleSec = 3,
    [string]$Pages = 'scene',
    [string]$QtBin = 'C:\Qt\5.15.2\msvc2019_64\bin',
    [switch]$Raster,
    [switch]$NoLaunch,
    [switch]$KeepOpen,
    [switch]$KeepFull
)
$ErrorActionPreference = 'Stop'
if ($NoLaunch -or $KeepOpen -or $KeepFull) {
    throw 'Background capture uses a separate, automatically closed instance; NoLaunch/KeepOpen/KeepFull are unsupported.'
}
$root = Split-Path -Parent $PSScriptRoot
$exePath = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $root $Exe }
$outPath = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
$outPath = Join-Path $outPath $Tag
if (-not (Test-Path -LiteralPath $exePath)) { throw "Executable not found: $exePath" }
# Do not launch an old binary that would ignore the background requirement and open a desktop window.
$binary = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exePath))
if (-not $binary.Contains('learnQT-background-v1')) { throw 'Rebuild learnQT: this executable does not support isolated background testing.' }
$binary = $null
New-Item -ItemType Directory -Force -Path $outPath | Out-Null
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $exePath
$start.WorkingDirectory = $root
$start.UseShellExecute = $false
$start.CreateNoWindow = $true
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
if ($QtBin -and (Test-Path -LiteralPath $QtBin)) { $start.Environment['PATH'] = $QtBin + ';' + $env:PATH }
if ($ScaleFactor) {
    $start.Environment['QT_SCALE_FACTOR'] = $ScaleFactor
    $start.Environment['QT_AUTO_SCREEN_SCALE_FACTOR'] = '0'
}
$arguments = @('--background-test', '--capture-ui', $outPath, '--capture-pages', $Pages,
    '--capture-width', "$Width", '--capture-height', "$Height", '--capture-warmup', "$($SettleSec * 1000)")
if ($Scene) { $arguments += @('--scene', $Scene) }
if ($Raster) { $arguments += '--capture-raster' }
foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
$process = [Diagnostics.Process]::new()
$process.StartInfo = $start
try {
    if (-not $process.Start()) { throw 'Cannot start background capture.' }
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $timedOut = -not $process.WaitForExit($StartupTimeoutSec * 1000)
    if ($timedOut) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    [IO.File]::WriteAllText((Join-Path $outPath 'stdout.log'), $stdout.GetAwaiter().GetResult())
    [IO.File]::WriteAllText((Join-Path $outPath 'stderr.log'), $stderr.GetAwaiter().GetResult())
    if ($timedOut) { throw 'Background capture timed out; its test process tree was terminated.' }
    if ($process.ExitCode -ne 0) { throw "Background capture failed ($($process.ExitCode)); see $outPath\stderr.log" }
    Write-Output "Background capture saved: $outPath"
} finally {
    $process.Dispose()
}
