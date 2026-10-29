# Удаление Samplit. Папка данных (настройки, свои пресеты и миксы) остаётся.
$ErrorActionPreference = 'Stop'

$running = Get-Process -Name 'Cubase*', 'Samplit', 'reaper' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host ('Закройте программы: ' + (($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', ') + ' и запустите удаление снова.')
    exit 1
}

$data = Join-Path $env:APPDATA 'Samplit'
$paths = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\Samplit.vst3'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Samplit'),
    (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Samplit.lnk'),
    (Join-Path $data 'packs'),
    (Join-Path $data 'presets'),
    (Join-Path $data 'manifests')
)
foreach ($p in $paths) {
    if (Test-Path $p) { Remove-Item $p -Recurse -Force; Write-Host "удалено: $p" }
}
Write-Host ''
Write-Host "Готово. Настройки остались в $data (удалите папку вручную, если она не нужна)."
