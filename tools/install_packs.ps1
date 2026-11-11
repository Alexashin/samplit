# Сборка пакетов библиотеки Studio600 и установка их на этот компьютер вместе с токеном лицензии.
#
#   .\tools\install_packs.ps1                       все пакеты из sample_library\packs-src (кроме unverified), бессрочный токен
#   .\tools\install_packs.ps1 -All                  вместе с studio600-unverified (непроверенные удары)
#   .\tools\install_packs.ps1 -Days 120             токен на срок вместо бессрочного
#   .\tools\install_packs.ps1 -Packs studio600-hat,studio600-ride
#   .\tools\install_packs.ps1 -SkipBuild            только токен (пакеты уже собраны)
#   .\tools\install_packs.ps1 -NoRegister -OutDir D:\tmp\packs
#                                                   только собрать и проверить пакеты в OutDir: без секретов, сервера лицензий и токена,
#                                                   одноразовые ключи в OutDir\keys (для проверки пересборки)
#
# Что делает:
#   1. samplit-packer build: пакет .splpack в %APPDATA%\Samplit\packs. Ключ пакета создаётся при первой сборке
#      и лежит в каталоге секретов сервера лицензий (~\.samplit-license\packs), дальше переиспользуется.
#   2. admin.py add-pack-key: ключ регистрируется на сервере лицензий (сервер выдаёт его в токенах).
#   3. make_token.py: токен на этот компьютер на все установленные пакеты, в %APPDATA%\Samplit\license.token.
#
# Нужно: собранный Release (.\tools\build.ps1 -Config Release), каталог секретов (admin.py init), venv сервера
# лицензий (tools\license_server\.venv). Сэмплы библиотеки в sample_library\00-Studio600ru-Drum-LIB.
param(
    [string[]]$Packs = @(),
    [int]$Days = 0,         # 0: бессрочный токен
    [switch]$All,
    [switch]$SkipBuild,
    [switch]$NoRegister,    # только сборка и проверка: ключи одноразовые, без регистрации и токена
    [string]$OutDir = '',   # куда писать .splpack (по умолчанию %APPDATA%\Samplit\packs)
    [string]$User = $env:USERNAME   # имя владельца токена
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
$packsDir = if ($OutDir) { $OutDir } else { Join-Path $data 'packs' }
if ($NoRegister -and -not $OutDir) { throw '-NoRegister требует -OutDir (в установленную папку собирать без регистрации нельзя)' }
# Ключи: в режиме -NoRegister одноразовые рядом с пакетами, иначе в каталоге секретов
$keysDir = if ($NoRegister) { Join-Path $OutDir 'keys' } else { Join-Path $secrets 'packs' }

$needs = @($packer, $library, $sources)
if (-not $NoRegister) { $needs += @($python, (Join-Path $secrets 'signing.seed')) }
foreach ($need in $needs) {
    if (-not (Test-Path $need)) { throw "не найдено: $need" }
}

New-Item -ItemType Directory -Force $packsDir, $keysDir | Out-Null

$manifests = Get-ChildItem $sources -Filter '*.json' | Sort-Object Name
# studio600-unverified по умолчанию не собираем: основная установка его не берёт, он нужен
# режиму с непроверенными ударами (-All). Повреждённые файлы библиотеки (TOMS-Pearl-Masters/A2/0-397.wav, 0-398.wav) вырезаны
# в tools\kit-studio600.json (excludeFiles)
if ($Packs.Count -gt 0) { $manifests = $manifests | Where-Object { $Packs -contains $_.BaseName } }
elseif (-not $All) { $manifests = $manifests | Where-Object { $_.BaseName -ne 'studio600-unverified' } }
if (-not $manifests) { throw 'нет манифестов для сборки' }

foreach ($m in $manifests) {
    $id = $m.BaseName
    $out = Join-Path $packsDir "$id.splpack"
    $keyFile = Join-Path $keysDir "$id.key"

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

    if ($NoRegister) { continue }
    & $python (Join-Path $root 'tools\license_server\admin.py') add-pack-key --pack-id $id --key-file $keyFile | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "регистрация ключа $id не прошла" }
}

if ($NoRegister) {
    Write-Host "Готово (без регистрации и токена): $packsDir"
    return
}

# Токен на все пакеты, которые лежат в папке и чей ключ есть в секретах
$installed = Get-ChildItem $packsDir -Filter '*.splpack' |
    Where-Object { Test-Path (Join-Path $secrets "packs\$($_.BaseName).key") } | ForEach-Object { $_.BaseName }
$fingerprint = (& $packer fingerprint).Trim()
$term = @(if ($Days -gt 0) { '--days', "$Days" } else { '--perpetual' })
& $python (Join-Path $root 'tools\make_token.py') --fingerprint $fingerprint --packs ($installed -join ',') `
    --user $User @term --out (Join-Path $data 'license.token')
if ($LASTEXITCODE -ne 0) { throw 'выпуск токена не прошёл' }

Write-Host "Готово: $data"
Write-Host ("Пакеты: " + ($installed -join ', '))
