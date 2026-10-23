# Код этого компьютера для выпуска лицензии Samplit. Считается так же, как в плагине (core/src/License.cpp):
# win:<MachineGuid>:<серийный номер тома Windows, hex без ведущих нулей>.
# Печатает код, кладёт его в буфер обмена и в файл «Код компьютера Samplit.txt» на рабочем столе.
$ErrorActionPreference = 'Stop'

$guid = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Cryptography' -Name MachineGuid).MachineGuid.ToLower()
$drive = (Split-Path $env:SystemRoot -Qualifier)
$serial = (Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$drive'").VolumeSerialNumber
if (-not $guid -or -not $serial) { throw 'не удалось прочитать код компьютера' }
$code = 'win:' + $guid + ':' + $serial.ToLower().TrimStart('0')

$file = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Код компьютера Samplit.txt'
[IO.File]::WriteAllText($file, $code + "`r`n", (New-Object Text.UTF8Encoding($false)))
try { Set-Clipboard -Value $code } catch { }

Write-Host ''
Write-Host 'Код этого компьютера:'
Write-Host "  $code"
Write-Host ''
Write-Host 'Он скопирован в буфер обмена и сохранён на рабочем столе в файле'
Write-Host '«Код компьютера Samplit.txt». Отправьте код Артёму: он выпустит файл лицензии.'
