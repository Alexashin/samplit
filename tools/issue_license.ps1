# Выпуск файла лицензии Samplit без сервера (локальная лицензия, временно до запуска сервера).
#
#   .\tools\issue_license.ps1 -User "Алексашин А.А."
#       бессрочная лицензия на ЭТОТ компьютер, сразу ставится в %APPDATA%\Samplit\license.token
#   .\tools\issue_license.ps1 -Code "win:...:..." -User "Имя Фамилия"
#       бессрочная лицензия на чужой компьютер по присланному коду: файл dist\licenses\<имя>\license.token,
#       его отправляют человеку, он открывает его кнопкой «Загрузить лицензию» в окне плагина
#   -Days 30    лицензия на срок вместо бессрочной
#
# В лицензию идут все пакеты, ключи которых есть в каталоге секретов (~\.samplit-license, см. install_packs.ps1).
param(
    [string]$Code = '',
    [Parameter(Mandatory = $true)][string]$User,
    [int]$Days = 0,
    [string]$Out = ''
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$env:PYTHONIOENCODING = 'utf-8'

$python = Join-Path $root 'tools\license_server\.venv\Scripts\python.exe'
$packer = Join-Path $root 'build\Release\tools\packer\samplit-packer_artefacts\Release\samplit-packer.exe'

$thisMachine = [string]::IsNullOrWhiteSpace($Code)
if ($thisMachine) {
    $Code = (& $packer fingerprint).Trim()
    if (-not $Out) { $Out = Join-Path $env:APPDATA 'Samplit\license.token' }
}
elseif (-not $Out) {
    $safe = ($User -replace '[^\w\.\- ]', '').Trim()
    if (-not $safe) { $safe = 'user' }
    $Out = Join-Path $root "dist\licenses\$safe\license.token"
}
New-Item -ItemType Directory -Force (Split-Path $Out) | Out-Null

$term = @(if ($Days -gt 0) { '--days', "$Days" } else { '--perpetual' })
$id = 'L-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
& $python (Join-Path $root 'tools\make_token.py') --fingerprint $Code --user $User --id $id @term --out $Out
if ($LASTEXITCODE -ne 0) { throw 'выпуск лицензии не прошёл' }

if ($thisMachine) { Write-Host 'Лицензия установлена на этот компьютер. Откройте плагин или нажмите «Обновить» в окне лицензии.' }
else { Write-Host "Отправьте файл $Out. Его открывают кнопкой «Загрузить лицензию» в окне плагина." }
