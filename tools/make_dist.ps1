# Дистрибутив Samplit для передачи: плагин VST3, отдельное приложение, пакеты, установщик, инструкция.
#
#   .\tools\make_dist.ps1                          dist\Samplit-<версия>\ и dist\Samplit-<версия>.zip
#   .\tools\make_dist.ps1 -NoZip
#
# Берёт Release-сборку (.\tools\build.ps1 -Config Release) и пакеты, установленные на этом компьютере
# (.\tools\install_packs.ps1). Лицензия отложена: в дистрибутив она не входит, звук берётся из папки WAV.
param(
    [switch]$NoZip,
    [switch]$Lite       # без пакетов: плагин берёт звук из папки с WAV (кнопка «Моя библиотека…»), архив в несколько МБ
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

$version = ([regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\(Samplit VERSION ([0-9.]+)')).Groups[1].Value
$artefacts = Join-Path $root 'build\Release\plugin\Samplit_artefacts\Release'
$packs = Join-Path $env:APPDATA 'Samplit\packs'
$out = Join-Path $root "dist\Samplit-$version"

foreach ($need in @((Join-Path $artefacts 'VST3\Samplit.vst3'), (Join-Path $artefacts 'Standalone\Samplit.exe')) + $(if (-not $Lite) { $packs }) + (Join-Path $root 'sample_library\packs-src')) {
    if (-not (Test-Path $need)) { throw "не найдено: $need" }
}

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
$setup = Join-Path $out 'setup'
New-Item -ItemType Directory -Force (Join-Path $out 'Plugin'), (Join-Path $out 'Data\packs'), $setup | Out-Null

Copy-Item (Join-Path $artefacts 'VST3\Samplit.vst3') (Join-Path $out 'Plugin') -Recurse
Copy-Item (Join-Path $artefacts 'Standalone\Samplit.exe') (Join-Path $out 'Plugin')
# Пакет studio600-unverified нужен только служебному режиму (SAMPLIT_SERVICE), в архив не идёт
if (-not $Lite) { Get-ChildItem (Join-Path $packs '*.splpack') -Exclude 'studio600-unverified*' | Copy-Item -Destination (Join-Path $out 'Data\packs') }
# Исходные манифесты (раскладка ударов без звука): нужны режиму «Моя библиотека…»
New-Item -ItemType Directory -Force (Join-Path $out 'Data\manifests') | Out-Null
Get-ChildItem (Join-Path $root 'sample_library\packs-src\*.json') -Exclude 'studio600-unverified*' | Copy-Item -Destination (Join-Path $out 'Data\manifests')
Copy-Item (Join-Path $root 'tools\dist\install.ps1'), (Join-Path $root 'tools\dist\uninstall.ps1') $setup
Copy-Item (Join-Path $root 'tools\dist\img') $setup -Recurse
# Карты ударных Cubase (python tools/gen_drum_map.py)
New-Item -ItemType Directory -Force (Join-Path $out 'Data\drum-maps') | Out-Null
Copy-Item (Join-Path $root 'tools\dist\drum-maps\*.drm') (Join-Path $out 'Data\drum-maps')
Copy-Item (Join-Path $root 'tools\dist\guide.html') (Join-Path $out 'Инструкция.html')

# Батники только с ASCII внутри: cmd читает их в кодировке консоли
$cmd = "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0setup\{0}`"`r`npause`r`n"
[IO.File]::WriteAllText((Join-Path $out 'Установить.cmd'), ($cmd -f 'install.ps1'), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $out 'Удалить.cmd'), ($cmd -f 'uninstall.ps1'), [Text.Encoding]::ASCII)
# Инструкция в PDF: tools\dist\Инструкция.pdf собирается из guide.html (docs/delivery.md)
$pdf = Join-Path $root 'tools\dist\Инструкция.pdf'
# PDF собирается вручную из guide.html и может отстать от него: в лёгкий дистрибутив не кладём
if ((Test-Path $pdf) -and -not $Lite) { Copy-Item $pdf (Join-Path $out 'Инструкция.pdf') }

$size = (Get-ChildItem $out -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("Дистрибутив: $out ({0:N0} МБ)" -f $size)

if (-not $NoZip) {
    $zip = "$out.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    # Имена по-русски: bsdtar пишет их без флага UTF-8 (в 7-Zip кракозябры), поэтому упаковывает Python
    python (Join-Path $root 'tools\zip_dist.py') $out $zip
    if ($LASTEXITCODE -ne 0) { throw 'не удалось собрать архив' }
    Write-Host ("Архив: $zip ({0:N0} МБ)" -f ((Get-Item $zip).Length / 1MB))
}
