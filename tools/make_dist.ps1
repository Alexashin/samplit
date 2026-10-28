# Дистрибутив Samplit для передачи: плагин VST3, отдельное приложение, пакеты, пресеты, установщик, инструкция.
#
#   .\tools\make_dist.ps1                          dist\Samplit-<версия>\ и dist\Samplit-<версия>.zip
#   .\tools\make_dist.ps1 -License <license.token> положить в дистрибутив готовый файл лицензии
#   .\tools\make_dist.ps1 -LicenseFolder <папка>   файлы лицензии (license.token, license.key.txt) в папку «Лицензия»:
#                                                  установщик их не ставит, лицензию вводят в плагине вручную
#   .\tools\make_dist.ps1 -NoZip
#
# Берёт Release-сборку (.\tools\build.ps1 -Config Release) и пакеты, установленные на этом компьютере
# (.\tools\install_packs.ps1). Лицензию для чужого компьютера выпускает tools\issue_license.ps1.
param(
    [string]$License = '',
    [string]$LicenseFolder = '',
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
New-Item -ItemType Directory -Force (Join-Path $out 'Plugin'), (Join-Path $out 'Data\packs'), (Join-Path $out 'Data\presets\mixes'), $setup | Out-Null

Copy-Item (Join-Path $artefacts 'VST3\Samplit.vst3') (Join-Path $out 'Plugin') -Recurse
Copy-Item (Join-Path $artefacts 'Standalone\Samplit.exe') (Join-Path $out 'Plugin')
# Пакет studio600-unverified нужен только служебным пресетам «[Проверка]», которые в архив не идут
Get-ChildItem (Join-Path $packs '*.splpack') -Exclude 'studio600-unverified*' | Copy-Item -Destination (Join-Path $out 'Data\packs')
# Служебные «full» (спорные удары) остаются только у нас
Get-ChildItem (Join-Path $root 'tools\presets\*.json') -Exclude 'studio600-full-*' | Copy-Item -Destination (Join-Path $out 'Data\presets')
Copy-Item (Join-Path $root 'tools\presets\mixes\*.json') (Join-Path $out 'Data\presets\mixes')
Copy-Item (Join-Path $root 'tools\dist\install.ps1'), (Join-Path $root 'tools\dist\uninstall.ps1'),
    (Join-Path $root 'tools\dist\fingerprint.ps1'), (Join-Path $root 'tools\dist\license.ps1') $setup
Copy-Item (Join-Path $root 'tools\dist\img') $setup -Recurse
Copy-Item (Join-Path $root 'tools\dist\guide.html') (Join-Path $out 'Инструкция.html')

# Батники только с ASCII внутри: cmd читает их в кодировке консоли
$cmd = "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0setup\{0}`"`r`npause`r`n"
[IO.File]::WriteAllText((Join-Path $out 'Установить.cmd'), ($cmd -f 'install.ps1'), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $out 'Удалить.cmd'), ($cmd -f 'uninstall.ps1'), [Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $out 'Код компьютера.cmd'), ($cmd -f 'fingerprint.ps1'), [Text.Encoding]::ASCII)
# Файл лицензии перетаскивают на этот батник: его путь приходит в %1
$lic = "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0setup\license.ps1`" %1`r`npause`r`n"
[IO.File]::WriteAllText((Join-Path $out 'Установить лицензию.cmd'), $lic, [Text.Encoding]::ASCII)

if ($License) {
    if (-not (Test-Path $License)) { throw "нет файла лицензии: $License" }
    Copy-Item $License (Join-Path $out 'license.token')
}

if ($LicenseFolder) {
    if (-not (Test-Path $LicenseFolder)) { throw "нет папки лицензии: $LicenseFolder" }
    $licOut = Join-Path $out 'Лицензия'
    New-Item -ItemType Directory -Force $licOut | Out-Null
    Copy-Item (Join-Path $LicenseFolder 'license.token'), (Join-Path $LicenseFolder 'license.key.txt') $licOut
}

# Инструкция в PDF: tools\dist\Инструкция.pdf собирается из guide.html (docs/delivery.md)
$pdf = Join-Path $root 'tools\dist\Инструкция.pdf'
if (Test-Path $pdf) { Copy-Item $pdf (Join-Path $out 'Инструкция.pdf') }

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
