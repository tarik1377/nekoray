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

# ИМЕНОВАННЫЕ ИСКЛЮЧЕНИЯ — по одному, с причиной.
#
# Сторож без ложных срабатываний удерживает запрет; сторож, который кричит на
# заведомо безобидное, перестают читать — и тогда он не удерживает ничего.
# Поэтому каждое исключение здесь названо парой «файл + строка» и объяснено.
# Добавлять сюда что-то по принципу «ну это же не текст» нельзя: если строка
# может дойти до человека, ей здесь не место.
$allowed = @(
    # Имена полей внутри запечатанного device.dat. Человеку не показываются
    # нигде и никогда — DeviceCredentials::Field() отдаёт их значения только
    # тому, кто сейчас же передаст их движку через окружение.
    @{ file = 'DeviceCredentials.cpp'; text = 'bucket' },
    @{ file = 'DeviceCredentials.cpp'; text = 'endpoint' }
)

function Check($where, $text) {
    $t = $text.Trim()
    foreach ($a in $allowed) {
        if ($where -eq $a.file -and $t -eq $a.text) { return }
    }
    foreach ($b in $banned) {
        if ($text -match $b.rx) {
            $script:hits += [PSCustomObject]@{ Where = $where; What = $b.why; Text = $t }
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

# 2. Строки, видимые человеку.
#
# ЗДЕСЬ БЫЛО ТРИ ДЫРЫ, И ВСЕ ТРИ УЖЕ ЗАСЕЛЕНЫ ЖИВЫМ КОДОМ. Сторож держит запрет
# ровно настолько, насколько проверяет, и каждая непроверенная форма записи —
# это место, куда объяснение устройства канала уедет людям при зелёном стороже.
#
#   1) Проверялся только ПЕРВЫЙ кусок внутри tr(). Длинные фразы в этом проекте
#      почти всегда разбиты на несколько литералов подряд, и всё, что после
#      первого, не смотрел никто.
#   2) Смотрелся только tr(). Мимо шли QStringLiteral и голые литералы в
#      MW_show_log, MessageBox и setText — а туда пишут ничуть не реже.
#   3) Правило пропуска путей было записано как [\/], то есть совпадало только с
#      косой чертой и НИКОГДА не срабатывало на Windows, где разделитель
#      обратный. Каталоги qtsdk и 3rdparty на самом деле проверялись всегда —
#      просто узкая проверка ничего в них не находила.
#
# Проверка идёт ПО МЕСТУ ВЫЗОВА, а не по всем литералам подряд. Это намеренно:
# имена переменных окружения, поля запечатанного блоба и образцы в наборах
# проверок содержат запрещённые слова ПО ДЕЛУ и человеку не показываются. Сторож,
# который кричит на них, перестают читать — а тогда он не удерживает ничего.
$skip = '[\\/](build|build-clean|3rdparty|qtsdk|libs|deployment|test)[\\/]'

# Всё, что доносит текст до человека.
$calls = 'tr|QStringLiteral|MW_show_log|MW_show_log_ext|setText|setToolTip|' +
         'setInformativeText|setWindowTitle|MessageBoxWarning|MessageBoxInfo|' +
         'setPlaceholderText|addButton'

function Get-ShownLiterals([string]$src) {
    $out = New-Object System.Collections.Generic.List[string]
    foreach ($m in [regex]::Matches($src, "($calls)\s*\(([^;]{0,700})", 'Singleline')) {
        # Соседние литералы склеиваются: "а" "б" — это одна фраза, и запрещённое
        # слово может оказаться разорванным ровно по границе между ними.
        $arg = [regex]::Replace($m.Groups[2].Value, '"[ 	
]*"', '')
        foreach ($lit in [regex]::Matches($arg, '"([^"]*)"')) {
            $out.Add($lit.Groups[1].Value)
        }
    }
    return $out
}

Get-ChildItem -Path $root -Include *.cpp, *.h, *.hpp -Recurse -File |
    Where-Object { $_.FullName -notmatch $skip } |
    ForEach-Object {
        $name = $_.Name
        foreach ($lit in (Get-ShownLiterals (ReadUtf8 $_.FullName))) { Check $name $lit }
    }

# 2b. Ядро печатает человеку через ret.Error — его текст виден в диалоге.
Get-ChildItem -Path (Join-Path $root 'go') -Include *.go -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch "[\\/](gen|vendor)[\\/]" -and $_.Name -notmatch '_test[.]go$' } |
    ForEach-Object {
        $name = $_.Name
        $src = ReadUtf8 $_.FullName
        foreach ($m in [regex]::Matches($src, '"([^"]*)"')) { Check $name $m.Groups[1].Value }
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
