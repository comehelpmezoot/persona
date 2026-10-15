<#
.SYNOPSIS
  Builds Persona Shell for Windows (Release by default) and deploys the Qt runtime next to the exe.

.EXAMPLE
  .\build.ps1 -QtDir C:\Qt\6.8.2\msvc2022_64
  .\build.ps1 -QtDir C:\Qt\6.8.2\msvc2022_64 -Assets D:\src\Persona-Quickshell\Assets -Run
  .\build.ps1 -NoSmtc          # if the C++/WinRT media-controls part gives you trouble
#>
param(
    [string]$QtDir = $env:PERSONA_QT_DIR,
    [string]$Assets = "",
    [ValidateSet("Release", "Debug")][string]$Config = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [switch]$NoSmtc,
    [switch]$Run
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# --- find Qt ---------------------------------------------------------------------------------
if (-not $QtDir) {
    $candidate = Get-ChildItem "C:\Qt" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^6\.' } | Sort-Object Name -Descending |
        ForEach-Object { Get-ChildItem $_.FullName -Directory -ErrorAction SilentlyContinue } |
        Where-Object { $_.Name -like 'msvc*_64' } | Select-Object -First 1
    if ($candidate) { $QtDir = $candidate.FullName }
}
if (-not $QtDir -or -not (Test-Path (Join-Path $QtDir "bin\windeployqt.exe"))) {
    throw "Qt 6 (MSVC 64-bit kit) not found. Pass -QtDir, e.g. -QtDir C:\Qt\6.8.2\msvc2022_64"
}
Write-Host "Qt:      $QtDir"

# --- configure ----------------------------------------------------------------------------------
$cmakeArgs = @("-S", ".", "-B", "build", "-G", $Generator, "-A", "x64", "-DCMAKE_PREFIX_PATH=$QtDir")
if ($Assets) {
    if (-not (Test-Path (Join-Path $Assets "p3r imgs\bg.png"))) { throw "'$Assets' does not look like the Persona Assets folder (p3r imgs\bg.png missing)." }
    $cmakeArgs += "-DPERSONA_ASSETS_DIR=$((Resolve-Path $Assets).Path)"
}
if ($NoSmtc) { $cmakeArgs += "-DPERSONA_ENABLE_SMTC=OFF" }

& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

# --- build, test, deploy ------------------------------------------------------------------------
& cmake --build build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

& ctest --test-dir build -C $Config --output-on-failure
if ($LASTEXITCODE -ne 0) { Write-Warning "Logic tests failed (the program may still run)." }

& cmake --build build --config $Config --target deploy
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

$exe = Join-Path $PSScriptRoot "build\$Config\PersonaShell.exe"
Write-Host ""
Write-Host "Done: $exe"
Write-Host "Log file:    $env:APPDATA\PersonaShell\log.txt"
Write-Host "Config file: $env:APPDATA\PersonaShell\config.json"
if ($Run) { & $exe }
