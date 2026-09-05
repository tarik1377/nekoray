# Лицензии и уведомления

GreenRhythm распространяется на условиях **GPL-3.0**. Полный текст лицензии —
в файле `LICENSE` рядом с программой.

**Исходный код:** https://github.com/tarik1377/nekoray

Это форк [nekoray](https://github.com/MatsuriDayo/nekoray). Мы обязаны отдавать
исходный код той сборки, которую вы получили, и отдаём: страница каждого
выпуска содержит ссылку на коммит, из которого он собран.

## Что ещё внутри

| Компонент | Лицензия | Исходный код |
|---|---|---|
| Qt 6 | LGPL-3.0 | https://code.qt.io/cgit/qt/qtbase.git |
| sing-box | GPL-3.0 | https://github.com/SagerNet/sing-box |
| Xray-core (`xray`) | MPL-2.0 | https://github.com/XTLS/Xray-core |
| OpenSSL (`libcrypto`, `libssl`) | Apache-2.0 | https://github.com/openssl/openssl |
| wintun (`wintun.dll`, только Windows) | GPL-2.0 / собственная | https://www.wintun.net/ |
| qrcodegen (`3rdparty/qrcodegen.cpp`) | MIT | https://www.nayuki.io/page/qr-code-generator-library |
| Qv2ray (`3rdparty/qv2ray/`) | GPL-3.0 | https://github.com/Qv2ray/Qv2ray |
| QHotkey | BSD-3-Clause | https://github.com/Skycoder42/QHotkey |
| zxing-cpp | Apache-2.0 | https://github.com/nu-book/zxing-cpp |
| yaml-cpp | MIT | https://github.com/jbeder/yaml-cpp |
| protobuf | BSD-3-Clause | https://github.com/protocolbuffers/protobuf |

Три первых из этого списка собираются прямо в наш бинарь из каталога
`3rdparty/`, а не подключаются готовой библиотекой: qrcodegen рисует QR-код
подписки, семь файлов из Qv2ray отвечают за редактор конфигурации, автодополнение
и чтение баз geosite. Их не было в этой таблице, хотя в бинарнике они есть.

`wintun.dll` лежит в дереве двоичным файлом. Чтобы по репозиторию можно было
проверить, что это именно он:

```
размер   427552 байт
sha256   e5da8447dc2c320edc0fc52fa01885c103de8c118481f683643cacc3220dafce
```

## Модуль усиленного обхода (скачивается по запросу, в пакет не входит)

Ничего из перечисленного здесь не поставляется вместе с клиентом. Файлы
скачиваются в `<каталог программы>/dpi` только если человек сам включил
«Усиленный обход» и согласился на загрузку. Источник один — релиз
`zapret-v72.13.zip` проекта bol-van; закреплённые размеры и суммы sha256 лежат
в `dpi/DpiBundle.cpp` и проверяются при установке.

| Компонент | Лицензия | Источник |
| --- | --- | --- |
| zapret / winws | MIT | https://github.com/bol-van/zapret |
| WinDivert (`WinDivert.dll`, `WinDivert64.sys`) | **LGPLv3** | https://github.com/basil00/WinDivert |
| Cygwin runtime (`cygwin1.dll`) | LGPLv3 с исключением | https://cygwin.com/licensing.html |

**WinDivert распространяется на выбор под LGPLv3 или GPLv2. Мы выбираем
LGPLv3** — и это не формальность. Из выбора следуют обязательства, которые
выполняются в коде, а не на словах:

- Библиотека поставляется **без изменений**, ровно теми файлами, что лежат в
  релизе. Ничего не пересобирается и не патчится.
- Человек вправе **заменить её на свою совместимую версию**. Поэтому суммы
  sha256 сверяются ТОЛЬКО при установке; при каждом запуске проверяется лишь
  наличие файла и его размер. Гейт по хешу на старте сделал бы замену
  невозможной, то есть нарушил бы §4(d).
- Исходный код WinDivert доступен по ссылке выше; текст LGPLv3 — там же.

Лицензия zapret кладётся рядом с бинарями файлом `LICENSE-zapret.txt` при
установке.

### Почему это отдельный ярус, а не часть программы

`WinDivert64.sys` — системный драйвер перехвата пакетов. Антивирусы штатно
помечают его как `RiskTool.Multi.WinDivert`: это известное свойство самого
драйвера, а не признак заражения. Положи мы его в общий пакет — карантин
получили бы все, включая тех, кому обход не нужен вовсе.

Драйвер поднимается только на время работы обхода и снимается сразу после.
Пока запущена игра с античитом из известного списка, модуль не запускается
вовсе и гаснет, если игра появилась при работающем обходе: бан отменить нельзя,
а решение о риске принимаем не мы.

## Данные о сетях

Наборы правил и базы адресов (`geoip.db`, `geosite.db`, `*.srs`) собираются
проектами [sing-geoip](https://github.com/SagerNet/sing-geoip) и
[sing-geosite](https://github.com/SagerNet/sing-geosite) на данных
[MaxMind GeoLite2](https://dev.maxmind.com/geoip/geolite2-free-geolocation-data)
и [v2fly/domain-list-community](https://github.com/v2fly/domain-list-community).

## Если вы получили эту программу и вам нужен исходный код

Он лежит по ссылке выше и доступен всем. Если по какой-то причине ссылка не
открывается, напишите в поддержку — мы обязаны его предоставить и предоставим.
