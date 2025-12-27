param(
  [string]$BuildDir = "build",
  [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
  [string]$Config = "Release",
  [string]$Generator = "Visual Studio 17 2022",
  [ValidateSet("Win32", "x64", "ARM64")]
  [string]$Arch = "x64",
  [switch]$Clean,
  [switch]$Run
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

function Is-MultiConfigGenerator([string]$gen) {
  if ($gen -match "Visual Studio") { return $true }
  if ($gen -match "Xcode") { return $true }
  if ($gen -match "Multi-Config") { return $true }
  return $false
}

if ($Clean -and (Test-Path $BuildDir)) {
  Remove-Item -Recurse -Force $BuildDir
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$cmakeArgs = @("-S", ".", "-B", $BuildDir, "-G", $Generator)

if (Is-MultiConfigGenerator $Generator) {
  $cmakeArgs += @("-A", $Arch)
} else {
  $cmakeArgs += @("-DCMAKE_BUILD_TYPE=$Config")
}

$cmakeArgsPretty = $cmakeArgs -join " "
Write-Host "==> Конфигурация: cmake $cmakeArgsPretty" -ForegroundColor Cyan
cmake @cmakeArgs

Write-Host "==> Сборка: cmake --build $BuildDir --config $Config" -ForegroundColor Cyan
cmake --build $BuildDir --config $Config

if ($Run) {
  $exeCandidate1 = Join-Path $BuildDir (Join-Path $Config "AlertCalendar.exe")
  $exeCandidate2 = Join-Path $BuildDir "AlertCalendar.exe"
  $exe = $null

  if (Test-Path $exeCandidate1) { $exe = $exeCandidate1 }
  elseif (Test-Path $exeCandidate2) { $exe = $exeCandidate2 }

  if (-not $exe) {
    throw "Не найден AlertCalendar.exe после сборки. Проверьте BuildDir/Config."
  }

  Write-Host "==> Запуск: $exe" -ForegroundColor Cyan
  & $exe
}


