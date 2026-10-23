# Установка файла лицензии Samplit. Файл license.token перетаскивают на «Установить лицензию.cmd»
# или передают путём первым аргументом.
param([string]$File = '')
$ErrorActionPreference = 'Stop'

if (-not $File) {
    # Без аргумента: берём license.token из папки дистрибутива или из «Загрузки»
    $candidates = @((Join-Path (Split-Path $PSScriptRoot) 'license.token'),
                    (Join-Path $env:USERPROFILE 'Downloads\license.token'))
    $File = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $File -or -not (Test-Path $File)) {
    Write-Host 'Файл лицензии не найден. Перетащите license.token на «Установить лицензию.cmd».'
    exit 1
}

# Проверяем, что это JSON с подписью, прежде чем заменять рабочую лицензию
try { $t = Get-Content $File -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $t = $null }
if (-not $t -or -not $t.payload -or -not $t.sig) {
    Write-Host 'Это не файл лицензии Samplit (нет payload и sig).'
    exit 1
}

$data = Join-Path $env:APPDATA 'Samplit'
New-Item -ItemType Directory -Force $data | Out-Null
Copy-Item $File (Join-Path $data 'license.token') -Force
Write-Host "Лицензия установлена: $data\license.token"
Write-Host 'Откройте плагин или нажмите «Обновить» в окне лицензии.'
