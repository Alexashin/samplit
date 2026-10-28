# Сборка пакетов библиотеки Studio600 и установка их на этот компьютер вместе с пресетами и токеном лицензии.
#
#   .\tools\install_packs.ps1                       все пакеты из sample_library\packs-src (кроме unverified), бессрочный токен
#   .\tools\install_packs.ps1 -All                  вместе с studio600-unverified (полные пресеты)
#   .\tools\install_packs.ps1 -Days 120             токен на срок вместо бессрочного
#   .\tools\install_packs.ps1 -Packs studio600-core,studio600-hat
#   .\tools\install_packs.ps1 -SkipBuild            только пресеты и токен (пакеты уже собраны)
#
# Что делает:
#   1. samplit-packer build: пакет .splpack в %APPDATA%\Samplit\packs. Ключ пакета создаётся при первой сборке
#      и лежит в каталоге секретов сервера лицензий (~\.samplit-license\packs), дальше переиспользуется.
#   2. admin.py add-pack-key: ключ регистрируется на сервере лицензий (сервер выдаёт его в токенах).
#   3. Пресеты из tools\presets и миксы из tools\presets\mixes в %APPDATA%\Samplit\presets.
#   4. make_token.py: токен на этот компьютер на все установленные пакеты, в %APPDATA%\Samplit\license.token.
#
# Нужно: собранный Release (.\tools\build.ps1 -Config Release), каталог секретов (admin.py init), venv сервера
# лицензий (tools\license_server\.venv). Сэмплы библиотеки в sample_library\00-Studio600ru-Drum-LIB.
param(
    [string[]]$Packs = @(),
    [int]$Days = 0,         # 0: бессрочный токен
    [switch]$All,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
$env:PYTHONIOENCODING = 'utf-8'
# Через powershell -File массив приходит одной строкой через запятую
$Packs = @($Packs | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$packer = Join-Path $root 'build\Release\tools\packer\samplit-packer_artefacts\Release\samplit-packer.exe'
$python = Join-Path $root 'tools\license_server\.venv\Scripts\python.exe'
$library = Join-Path $root 'sample_library\00-Studio600ru-Drum-LIB'
$sources = Join-Path $root 'sample_library\packs-src'
$secrets = if ($env:SAMPLIT_LICENSE_DIR) { $env:SAMPLIT_LICENSE_DIR } else { Join-Path $HOME '.samplit-license' }
$data = Join-Path $env:APPDATA 'Samplit'

foreach ($need in @($packer, $python, $library, $sources, (Join-Path $secrets 'signing.seed'))) {
    if (-not (Test-Path $need)) { throw "не найдено: $need" }
}

New-Item -ItemType Directory -Force (Join-Path $data 'packs'), (Join-Path $data 'presets'), (Join-Path $secrets 'packs') | Out-Null

$manifests = Get-ChildItem $sources -Filter '*.json' | Sort-Object Name
# studio600-unverified по умолчанию не собираем: основные пресеты его не берут (includeUnverified: false), он нужен
# полным пресетам (-All). Повреждённые файлы библиотеки (TOMS-Pearl-Masters/A2/0-397.wav, 0-398.wav) вырезаны
# в tools\kit-studio600.json (excludeFiles)
if ($Packs.Count -gt 0) { $manifests = $manifests | Where-Object { $Packs -contains $_.BaseName } }
elseif (-not $All) { $manifests = $manifests | Where-Object { $_.BaseName -ne 'studio600-unverified' } }
if (-not $manifests) { throw 'нет манифестов для сборки' }

foreach ($m in $manifests) {
    $id = $m.BaseName
    $out = Join-Path $data "packs\$id.splpack"
    $keyFile = Join-Path $secrets "packs\$id.key"

    if (-not $SkipBuild) {
        $keyArgs = if (Test-Path $keyFile) { @('--key-file', $keyFile) } else { @('--gen-key', $keyFile) }
        Write-Host "== $id"
        $started = Get-Date
        & $packer build $m.FullName --root $library --out $out @keyArgs
        if ($LASTEXITCODE -ne 0) { throw "сборка $id не прошла" }
        & $packer verify $out --key-file $keyFile
        if ($LASTEXITCODE -ne 0) { throw "проверка $id не прошла" }
        Write-Host ("   {0:N0} МБ за {1:N0} с" -f ((Get-Item $out).Length / 1MB), ((Get-Date) - $started).TotalSeconds)
    }

    & $python (Join-Path $root 'tools\license_server\admin.py') add-pack-key --pack-id $id --key-file $keyFile | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "регистрация ключа $id не прошла" }
}

Copy-Item (Join-Path $root 'tools\presets\*.json') (Join-Path $data 'presets') -Force
New-Item -ItemType Directory -Force (Join-Path $data 'presets\mixes') | Out-Null
Copy-Item (Join-Path $root 'tools\presets\mixes\*.json') (Join-Path $data 'presets\mixes') -Force

# Токен на все пакеты, которые лежат в папке и чей ключ есть в секретах
$installed = Get-ChildItem (Join-Path $data 'packs') -Filter '*.splpack' |
    Where-Object { Test-Path (Join-Path $secrets "packs\$($_.BaseName).key") } | ForEach-Object { $_.BaseName }
$fingerprint = (& $packer fingerprint).Trim()
$term = @(if ($Days -gt 0) { '--days', "$Days" } else { '--perpetual' })
& $python (Join-Path $root 'tools\make_token.py') --fingerprint $fingerprint --packs ($installed -join ',') `
    --user 'Алексашин А.А.' @term --out (Join-Path $data 'license.token')
if ($LASTEXITCODE -ne 0) { throw 'выпуск токена не прошёл' }

Write-Host "Готово: $data"
Write-Host ("Пакеты: " + ($installed -join ', '))
