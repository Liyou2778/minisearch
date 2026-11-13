# PowerShell entry point for the MSVC + Ninja Release build.
# The heavy lifting lives in build.cmd (pure cmd) so this script also works when
# the PowerShell execution policy blocks unsigned scripts:
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
#   (or just run .\build.cmd from cmd.exe)
[CmdletBinding()]
param(
    [string]$Vcvars = $env:VCVARS,
    [string]$CMake  = $env:CMAKE_BIN,
    [string]$Ninja  = $env:NINJA_BIN,
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

if ($Vcvars) { $env:VCVARS = $Vcvars }
if ($CMake)  { $env:CMAKE_BIN = $CMake }
if ($Ninja)  { $env:NINJA_BIN = $Ninja }
$env:BUILD_TYPE = $Config

if ($Clean -and (Test-Path "build")) { Remove-Item -Recurse -Force "build" }

Write-Host "== build minisearch ($Config) ==" -ForegroundColor Cyan
& cmd /c "`"$root\build.cmd`""
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }
Write-Host "== build OK ==" -ForegroundColor Green
