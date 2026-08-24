<#
.SYNOPSIS
    Сторож: тексты для человека не рассказывают, как устроен резервный канал.

.DESCRIPTION
    Владелец запретил описывать устройство резервного подключения в любом
    тексте, который видит посторонний. Запрет живуч ровно настолько, насколько
    его кто-то проверяет: одна «уточняющая» формулировка в подсказке — и
    объяснение уезжает людям вместе с выпуском.

    Проверяются ТОЛЬКО видимые строки:
      *.ui                 содержимое <string>
      *.cpp, *.h, *.hpp    литералы внутри tr(...)
      translations/*.ts    переводы

    Комментарии намеренно НЕ проверяются: в них про устройство канала писать
    можно и нужно, иначе следующий не поймёт, почему тут так сделано.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File support\Test-NoChannelSecrets.ps1
#>

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

# Кириллицу \w не покрывает, поэтому \S* — тот же набор, что у теста на сайте.
$banned = @(
    @{ rx = 'объектн\S*\s+хранилищ'; why = 'объектное хранилище' },
    @{ rx = 'облачн\S*\s+хранилищ';  why = 'облачное хранилище' },
    @{ rx = 'object\s*storage';      why = 'object storage' },
    @{ rx = '\bs3\b';                why = 'S3' },
    @{ rx = 'бакет';                 why = 'бакет' },
    @{ rx = '\bbucket\b';            why = 'bucket' },
    @{ rx = 'yandexcloud';           why = 'yandexcloud' },
    @{ rx = 'через\s+файл';          why = 'через файлы' }
)

$script:hits = @()

function Check($where, $text) {
    foreach ($b in $banned) {
        if ($text -match $b.rx) {
            $script:hits += [PSCustomObject]@{ Where = $where; What = $b.why; Text = $text.Trim() }
        }
    }
}

function ReadUtf8($path) {
    [System.Text.Encoding]::UTF8.GetString([System.IO.File]::ReadAllBytes($path))
}

# 1. Вёрстка
Get-ChildItem -Path (Join-Path $root 'ui') -Filter *.ui -Recurse -File | ForEach-Object {
    $name = $_.Name
    $xml = ReadUtf8 $_.FullName
    foreach ($m in [regex]::Matches($xml, '<string[^>]*>([^<]*)</string>')) {
        Check $name $m.Groups[1].Value
    }
}

# 2. Строки, видимые человеку: только внутри tr(...)
$skip = '[\/](build|3rdparty|qtsdk|libs|go|deployment)[\/]'
Get-ChildItem -Path $root -Include *.cpp, *.h, *.hpp -Recurse -File |
    Where-Object { $_.FullName -notmatch $skip } |
    ForEach-Object {
        $name = $_.Name
        $src = ReadUtf8 $_.FullName
        foreach ($m in [regex]::Matches($src, 'tr\s*\(\s*"([^"]*)"')) {
            Check $name $m.Groups[1].Value
        }
    }

# 3. Переводы
$tsDir = Join-Path $root 'translations'
if (Test-Path -LiteralPath $tsDir) {
    Get-ChildItem -Path $tsDir -Filter *.ts -File | ForEach-Object {
        $name = $_.Name
        $xml = ReadUtf8 $_.FullName
        foreach ($m in [regex]::Matches($xml, '<source>([^<]*)</source>')) {
            Check $name $m.Groups[1].Value
        }
        foreach ($m in [regex]::Matches($xml, '<translation[^>]*>([^<]*)</translation>')) {
            Check $name $m.Groups[1].Value
        }
    }
}

Write-Host ""
if ($script:hits.Count -eq 0) {
    Write-Host "  ок - в текстах для человека про устройство канала ничего нет" -ForegroundColor Green
    Write-Host ""
    exit 0
}

Write-Host "  НАЙДЕНО: тексты рассказывают, как устроен резервный канал" -ForegroundColor Red
foreach ($h in $script:hits) {
    $t = $h.Text
    if ($t.Length -gt 80) { $t = $t.Substring(0, 80) + "..." }
    Write-Host ("    {0,-26} {1,-20} {2}" -f $h.Where, $h.What, $t) -ForegroundColor Yellow
}
Write-Host ""
exit 1
