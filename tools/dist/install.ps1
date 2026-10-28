# Установка Samplit на компьютер пользователя. Запускается из «Установить.cmd» в папке дистрибутива,
# сам лежит в её подпапке setup. Права администратора не нужны: всё ставится в папки пользователя.
$ErrorActionPreference = 'Stop'
$src = Split-Path $PSScriptRoot

$vst3Dir = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
$appDir = Join-Path $env:LOCALAPPDATA 'Programs\Samplit'
$data = Join-Path $env:APPDATA 'Samplit'

Write-Host 'Установка Samplit'
Write-Host ''

$running = Get-Process -Name 'Cubase*', 'Samplit', 'reaper' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host ('Закройте программы: ' + (($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', '))
    Write-Host 'и запустите установку снова: пока плагин открыт, Windows не даёт заменить его файлы.'
    exit 1
}

New-Item -ItemType Directory -Force $vst3Dir, $appDir, (Join-Path $data 'packs'), (Join-Path $data 'presets'), (Join-Path $data 'presets\mixes') | Out-Null

Write-Host '1/4  Плагин VST3'
$vst3 = Join-Path $vst3Dir 'Samplit.vst3'
if (Test-Path $vst3) { Remove-Item $vst3 -Recurse -Force }
Copy-Item (Join-Path $src 'Plugin\Samplit.vst3') $vst3 -Recurse

Write-Host '2/4  Отдельное приложение и ярлык на рабочем столе'
Copy-Item (Join-Path $src 'Plugin\Samplit.exe') (Join-Path $appDir 'Samplit.exe') -Force
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Samplit.lnk'))
$link.TargetPath = Join-Path $appDir 'Samplit.exe'
$link.WorkingDirectory = $appDir
$link.Save()

Write-Host '3/4  Пакеты с сэмплами и пресеты (около 1 ГБ, подождите)'
Copy-Item (Join-Path $src 'Data\packs\*.splpack') (Join-Path $data 'packs') -Force
Copy-Item (Join-Path $src 'Data\presets\*.json') (Join-Path $data 'presets') -Force
Copy-Item (Join-Path $src 'Data\presets\mixes\*.json') (Join-Path $data 'presets\mixes') -Force

Write-Host '4/4  Лицензия'
$token = Join-Path $src 'license.token'
if (Test-Path $token) {
    Copy-Item $token (Join-Path $data 'license.token') -Force
    Write-Host '     файл лицензии из папки установлен'
}
else {
    Write-Host '     Лицензия вводится в плагине вручную. Откройте Samplit (окно лицензии откроется само) и либо'
    Write-Host '     вставьте ключ из файла license.key.txt в поле «Ключ лицензии», либо нажмите «Выбрать файл»'
    Write-Host '     и укажите license.token. Оба файла лежат в папке «Лицензия» рядом с установщиком.'
}

Write-Host ''
Write-Host 'Готово.'
Write-Host "  Плагин:     $vst3"
Write-Host "  Приложение: ярлык Samplit на рабочем столе"
Write-Host "  Данные:     $data"
