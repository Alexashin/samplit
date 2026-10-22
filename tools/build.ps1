# Сборка Samplit на Windows: подхватывает окружение Visual Studio, конфигурирует CMake (Ninja) и собирает.
#   .\tools\build.ps1                 Debug
#   .\tools\build.ps1 -Config Release
#   .\tools\build.ps1 -WithHost       заодно собрать JUCE AudioPluginHost
param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [switch]$WithHost,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio с нагрузкой "C++ desktop" не найдена' }

# vcvars64.bat сам вызывает vswhere, ему нужна его папка в PATH
$env:PATH += ";" + (Split-Path $vswhere)

# Загружаем переменные окружения из vcvars64.bat в текущий процесс
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
}

$build = Join-Path $root "build\$Config"
if ($Clean -and (Test-Path $build)) { Remove-Item $build -Recurse -Force }

$hostFlag = if ($WithHost) { 'ON' } else { 'OFF' }
cmake -S $root -B $build -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DSAMPLIT_BUILD_HOST=$hostFlag"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure не прошёл' }

cmake --build $build
if ($LASTEXITCODE -ne 0) { throw 'сборка не прошла' }

Write-Host "`nГотово: $build" -ForegroundColor Green
