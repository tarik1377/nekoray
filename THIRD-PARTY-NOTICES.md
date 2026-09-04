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

## Данные о сетях

Наборы правил и базы адресов (`geoip.db`, `geosite.db`, `*.srs`) собираются
проектами [sing-geoip](https://github.com/SagerNet/sing-geoip) и
[sing-geosite](https://github.com/SagerNet/sing-geosite) на данных
[MaxMind GeoLite2](https://dev.maxmind.com/geoip/geolite2-free-geolocation-data)
и [v2fly/domain-list-community](https://github.com/v2fly/domain-list-community).

## Если вы получили эту программу и вам нужен исходный код

Он лежит по ссылке выше и доступен всем. Если по какой-то причине ссылка не
открывается, напишите в поддержку — мы обязаны его предоставить и предоставим.
