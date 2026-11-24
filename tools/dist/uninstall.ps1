# Удаление Samplit Drummer (прежний плагин Samplit не трогается). Папка данных (настройки, свои пресеты и миксы) остаётся.
$ErrorActionPreference = 'Stop'

$running = Get-Process -Name 'Cubase*', 'Samplit Drummer', 'reaper' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host ('Закройте программы: ' + (($running | Select-Object -ExpandProperty ProcessName -Unique) -join ', ') + ' и запустите удаление снова.')
    exit 1
}

$data = Join-Path $env:APPDATA 'Samplit Drummer'
$paths = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3\Samplit Drummer.vst3'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Samplit Drummer'),
    (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Samplit Drummer.lnk'),
    (Join-Path $data 'packs'),
    (Join-Path $data 'presets'),
    (Join-Path $data 'manifests')
)
foreach ($p in $paths) {
    if (Test-Path $p) { Remove-Item $p -Recurse -Force; Write-Host "удалено: $p" }
}
Write-Host ''
Write-Host "Готово. Настройки остались в $data (удалите папку вручную, если она не нужна)."
