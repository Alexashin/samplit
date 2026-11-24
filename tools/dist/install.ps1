# Установка Samplit Drummer на компьютер пользователя. Запускается из «Установить.cmd» в папке дистрибутива,
# сам лежит в её подпапке setup. Права администратора не нужны: всё ставится в папки пользователя.
$ErrorActionPreference = 'Stop'
$src = Split-Path $PSScriptRoot

$vst3Dir = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$appDir = Join-Path $env:LOCALAPPDATA 'Programs\Samplit Drummer'
$data = Join-Path $env:APPDATA 'Samplit Drummer'
$oldData = Join-Path $env:APPDATA 'Samplit'   # данные прежнего плагина Samplit (он остаётся нетронутым)

Write-Host 'Установка Samplit Drummer'
Write-Host ''

$running = Get-Process -Name 'Cubase*', 'Samplit Drummer', 'reaper' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host ('Закройте программы: ' + (($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', '))
    Write-Host 'и запустите установку снова: пока плагин открыт, Windows не даёт заменить его файлы.'
    exit 1
}

New-Item -ItemType Directory -Force $vst3Dir, $appDir, (Join-Path $data 'packs'), (Join-Path $data 'manifests'), (Join-Path $data 'presets'), (Join-Path $data 'presets\mixes') | Out-Null

Write-Host '1/3  Плагин VST3'
$vst3 = Join-Path $vst3Dir 'Samplit Drummer.vst3'
if (Test-Path $vst3) { Remove-Item $vst3 -Recurse -Force }
Copy-Item (Join-Path $src 'Plugin\Samplit Drummer.vst3') $vst3 -Recurse

Write-Host '2/3  Отдельное приложение и ярлык на рабочем столе'
Copy-Item (Join-Path $src 'Plugin\Samplit Drummer.exe') (Join-Path $appDir 'Samplit Drummer.exe') -Force
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Samplit Drummer.lnk'))
$link.TargetPath = Join-Path $appDir 'Samplit Drummer.exe'
$link.WorkingDirectory = $appDir
$link.Save()

# Пакеты и лицензия, уже установленные для прежнего Samplit, не качаем заново: жёсткая ссылка (без лишнего места на диске), иначе копия
$newPacks = Join-Path $data 'packs'
if (-not (Get-ChildItem $newPacks -Filter *.splpack -ErrorAction SilentlyContinue) -and (Test-Path (Join-Path $oldData 'packs'))) {
    foreach ($f in Get-ChildItem (Join-Path $oldData 'packs') -Filter *.splpack) {
        $dst = Join-Path $newPacks $f.Name
        cmd /c mklink /H "$dst" "$($f.FullName)" 2>$null | Out-Null
        if (-not (Test-Path $dst)) { Copy-Item $f.FullName $dst }
    }
}
if (-not (Test-Path (Join-Path $data 'license.token')) -and (Test-Path (Join-Path $oldData 'license.token'))) { Copy-Item (Join-Path $oldData 'license.token') $data }

Write-Host '3/3  Описание ударов (манифесты) и пакеты, если они есть в дистрибутиве'
if (Test-Path (Join-Path $src 'Data\packs\*.splpack')) { Copy-Item (Join-Path $src 'Data\packs\*.splpack') (Join-Path $data 'packs') -Force }
# Манифесты для режима «Моя библиотека…» (звук из папки с WAV, без пакетов)
if (Test-Path (Join-Path $src 'Data\manifests')) { Copy-Item (Join-Path $src 'Data\manifests\*.json') (Join-Path $data 'manifests') -Force }
Copy-Item (Join-Path $src 'Data\presets\*.json') (Join-Path $data 'presets') -Force
Copy-Item (Join-Path $src 'Data\presets\mixes\*.json') (Join-Path $data 'presets\mixes') -Force

Write-Host ''
Write-Host 'Готово.'
Write-Host "  Плагин:     $vst3"
Write-Host "  Приложение: ярлык Samplit Drummer на рабочем столе"
Write-Host "  Данные:     $data"
