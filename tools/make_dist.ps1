# Дистрибутив Samplit для передачи: плагин VST3, отдельное приложение, пакеты, пресеты, установщик, инструкция.
#
#   .\tools\make_dist.ps1                          dist\Samplit-<версия>\ и dist\Samplit-<версия>.zip
#   .\tools\make_dist.ps1 -License <license.token> положить в дистрибутив готовый файл лицензии
#   .\tools\make_dist.ps1 -NoZip
#
# Берёт Release-сборку (.\tools\build.ps1 -Config Release) и пакеты, установленные на этом компьютере
# (.\tools\install_packs.ps1). Лицензию для чужого компьютера выпускает tools\issue_license.ps1.
param(
    [string]$License = '',
    [switch]$NoZip
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$version = ([regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(Samplit VERSION ([0-9.]+)')).Groups[1].Value
$artefacts = Join-Path $root 'build\Release\plugin\Samplit_artefacts\Release'
$packs = Join-Path $env:APPDATA 'Samplit\packs'
$out = Join-Path $root "dist\Samplit-$version"

foreach ($need in @((Join-Path $artefacts 'VST3\Samplit.vst3'), (Join-Path $artefacts 'Standalone\Samplit.exe'), $packs)) {
    if (-not (Test-Path $need)) { throw "не найдено: $need" }
}

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
$setup = Join-Path $out 'setup'
New-Item -ItemType Directory -Force (Join-Path $out 'Plugin'), (Join-Path $out 'Data\packs'), (Join-Path $out 'Data\presets'), $setup | Out-Null

Copy-Item (Join-Path $artefacts 'VST3\Samplit.vst3') (Join-Path $out 'Plugin') -Recurse
Copy-Item (Join-Path $artefacts 'Standalone\Samplit.exe') (Join-Path $out 'Plugin')
Copy-Item (Join-Path $packs '*.splpack') (Join-Path $out 'Data\packs')
Copy-Item (Join-Path $root 'tools\presets\*.json') (Join-Path $out 'Data\presets')
Copy-Item (Join-Path $root 'tools\dist\install.ps1'), (Join-Path $root 'tools\dist\uninstall.ps1') $setup
Copy-Item (Join-Path $root 'tools\dist\img') $setup -Recurse
Copy-Item (Join-Path $root 'tools\dist\guide.html') (Join-Path $out 'Инструкция.html')

# Батники только с ASCII внутри: cmd читает их в кодировке консоли
$cmd = "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0setup\{0}`"`r`npause`r`n"
[IO.File]::WriteAllText((Join-Path $out 'Установить.cmd'), ($cmd -f 'install.ps1'), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $out 'Удалить.cmd'), ($cmd -f 'uninstall.ps1'), [Text.Encoding]::ASCII)

if ($License) {
    if (-not (Test-Path $License)) { throw "нет файла лицензии: $License" }
    Copy-Item $License (Join-Path $out 'license.token')
}

$size = (Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Дистрибутив: $out ({0:N0} МБ)" -f $size)

if (-not $NoZip) {
    $zip = "$out.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    # Пакеты уже сжаты (FLAC), поэтому архив без сжатия: быстрее и того же размера
    Push-Location (Split-Path $out)
    try { tar -a -c -f $zip --options zip:compression=store (Split-Path $out -Leaf) }
    finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { throw 'не удалось собрать архив' }
    Write-Host ("Архив: $zip ({0:N0} МБ)" -f ((Get-Item $zip).Length / 1MB))
}
