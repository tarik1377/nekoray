#!/bin/bash
set -e

# Собрать готовый .app и образ диска для macOS.
#
# Запускается ТОЛЬКО на маке: macdeployqt и hdiutil входят в систему и в Xcode,
# заменителей у них нет.
#
# Архитектура задаётся снаружи (NKR_MACOS_ARCH = arm64 | amd64) и обязана
# совпадать с той, для которой собраны и приложение, и Go-бинари. Универсальную
# сборку мы намеренно НЕ делаем: Go таких бинарей не умеет, понадобился бы lipo
# на каждый файл плюс зависимости в двух архитектурах — три источника отказа
# ради одной ссылки на странице загрузки.

source libs/env_deploy.sh

ARCH="${NKR_MACOS_ARCH:-arm64}"
case "$ARCH" in
arm64 | amd64) ;;
*)
  echo "NKR_MACOS_ARCH must be arm64 or amd64, got: '$ARCH'" >&2
  exit 1
  ;;
esac

DEST=$DEPLOYMENT/macos-$ARCH
APP="$DEST/GreenRhythm.app"
# ВНУТРЬ Contents/MacOS, а не Contents/Resources. Приложение ищет соседние
# файлы через applicationDirPath(), а он на маке указывает именно в MacOS;
# положенное в Resources оно просто не найдёт — и скажет об этом не «файла нет»,
# а «ядро не запускается».
BIN="$APP/Contents/MacOS"

rm -rf "$DEST"
mkdir -p "$DEST"

#### приложение ####
# CMake с MACOSX_BUNDLE уже собрал бандл целиком.
cp -R "$BUILD/greenrhythm.app" "$APP"

#### Go-бинари ####
# Их кладёт libs/build_go.sh в deployment/macos-<arch>/ ДО этого шага.
GO_SRC="$DEPLOYMENT/macos-$ARCH-go"
if [ -d "$GO_SRC" ]; then
  cp "$GO_SRC/greenrhythm_core" "$BIN/"
  # launcher, а не updater: symlink updater -> launcher заводит сам main.cpp при
  # первом запуске. Файла launcher когда-то не собиралось вовсе, и симлинк
  # повисал битым — кнопка «Обновить» молча переставала работать.
  cp "$GO_SRC/launcher" "$BIN/"
  chmod +x "$BIN/greenrhythm_core" "$BIN/launcher"
else
  echo "Go binaries not found at $GO_SRC — build them first" >&2
  exit 1
fi

#### xray ####
# Версия и sha256 пинуются там же, где для Windows. Архитектура обязана совпасть
# со сборкой: файл не той архитектуры не откажет при выкладке — он откажет у
# человека, при первой попытке подключиться.
# Отсутствие файла — ОТКАЗ, а не предупреждение. Шаг CI его кладёт; если его
# нет, значит шаг сломался, и собранный без него бандл выглядит целым, а на
# части профилей молча не подключается. Такое лучше поймать здесь.
#
# Для сборки руками без внешнего ядра есть явный выключатель — но его надо
# набрать, то есть решение принимает человек, а не умолчание.
XRAY_SRC="$DEPLOYMENT/xray-macos-$ARCH"
if [ -f "$XRAY_SRC" ]; then
  cp "$XRAY_SRC" "$BIN/xray"
  chmod +x "$BIN/xray"
elif [ "$NKR_ALLOW_NO_XRAY" = "1" ]; then
  echo "[warn] xray for macos-$ARCH not found — собираем без него по явной просьбе" >&2
else
  echo "xray for macos-$ARCH not found at $XRAY_SRC" >&2
  echo "профили xhttp и Reality без него не подключатся;" >&2
  echo "для сборки без внешнего ядра задайте NKR_ALLOW_NO_XRAY=1" >&2
  exit 1
fi

#### значок ####
# Собирается здесь из существующего PNG, а не хранится двоичным файлом в
# репозитории: sips и iconutil входят в macOS, и лишний бинарь в дереве ничего
# не даёт. Имя обязано совпадать с MACOSX_BUNDLE_ICON_FILE в CMakeLists.
#
# Отсутствие значка — не мелочь: у неподписанного приложения система и так
# показывает предупреждение, и безымянный белый лист рядом с ним читается как
# «это точно вирус».
ICONSET="$DEST/greenrhythm.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
SRC_ICON="$SRC_ROOT/res/public/greenrhythm.png"
# Исходник 256x256, поэтому крупные размеры не выдумываем: увеличенное из
# меньшего выглядит мылом, и лучше честно отдать те размеры, что есть.
for sz in 16 32 64 128 256; do
  sips -z $sz $sz "$SRC_ICON" --out "$ICONSET/icon_${sz}x${sz}.png" >/dev/null
done
# Удвоенные варианты для экранов Retina: имя _NxN@2x означает картинку вдвое
# больше, поэтому берём готовый следующий размер.
cp "$ICONSET/icon_32x32.png" "$ICONSET/icon_16x16@2x.png"
cp "$ICONSET/icon_64x64.png" "$ICONSET/icon_32x32@2x.png"
cp "$ICONSET/icon_256x256.png" "$ICONSET/icon_128x128@2x.png"
rm -f "$ICONSET/icon_64x64.png"
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/greenrhythm.icns"
rm -rf "$ICONSET"
test -f "$APP/Contents/Resources/greenrhythm.icns"

#### шаблоны настроек ####
cp "$SRC_ROOT/res/public/greenrhythm.png" "$BIN/greenrhythm.png"
mkdir -p "$BIN/config/groups"
cp "$SRC_ROOT/res/config_template/groups/nekobox.json" "$BIN/config/groups/nekobox.json"

#### геобазы ####
# БЕЗ НИХ ПРИЛОЖЕНИЕ НЕ СОБИРАЕТ КОНФИГУРАЦИЮ ВООБЩЕ. db/ConfigBuilder.cpp
# проверяет наличие geoip.db и geosite.db раньше всего остального, и человек на
# любой профиль получает окно с отказом вместо подключения. То есть без этих
# двух файлов сборка бесполезна целиком, а выглядит рабочей.
#
# Кладутся в $BIN, а не в config/: FindCoreAsset ищет их рядом с исполняемым
# файлом, то есть в Contents/MacOS.
#
# Ошибки НЕ глушатся. Здесь стояло «2>/dev/null || true», и любое расхождение
# в путях прошло бы молча — ровно тот случай, когда собранное выглядит целым.
cp "$DEPLOYMENT/public_res/geoip.db" "$BIN/"
cp "$DEPLOYMENT/public_res/geosite.db" "$BIN/"

#### наборы правил ####
# Тоже без глушения: без них правила «российское — напрямую» и блокировка
# рекламы не действуют до первой докачки, а докачка идёт через сам туннель.
cp "$DEPLOYMENT/public_res/geosite-category-ads-all.srs" "$BIN/config/"
cp "$DEPLOYMENT/public_res/geoip-ru.srs" "$BIN/config/"

#### Qt ####
# ПОСЛЕ копирования всех бинарей: macdeployqt правит пути к библиотекам у того,
# что нашёл внутри бандла. Запущенный раньше, он не увидит ни ядро, ни xray, и
# приложение соберётся внешне целым, а упадёт при запуске у человека.
macdeployqt "$APP" -verbose=2

#### подпись «для себя» ####
#
# Настоящей подписи разработчика у нас нет, и этот шаг её не заменяет: система
# всё равно предупредит при первом запуске, и об этом сказано в
# docs/Run_macOS.md.
#
# Он закрывает ДРУГОЙ отказ, который выглядит куда хуже: macdeployqt правит пути
# к библиотекам через install_name_tool, а это ломает подпись, проставленную
# компоновщиком. Бандл со сломанной подписью система объявляет «повреждённым» и
# предлагает переместить в корзину — без варианта «всё равно открыть». Человек
# уверен, что скачал битый файл, и второго раза не будет.
#
# Ad-hoc подпись (-s -) ставится ПОСЛЕ macdeployqt и делает бандл целостным с
# точки зрения системы, не заявляя при этом никакого разработчика.
codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP" || {
  echo "бандл не проходит собственную проверку целостности" >&2
  exit 1
}

#### образ диска — С ПАПКОЙ «ПРОГРАММЫ» ####
#
# ПОЧЕМУ НЕ ПРОСТО .app В ОБРАЗЕ. Так на маке не делают, и дело не в красоте:
# запущенное прямо из образа приложение работает из тома ТОЛЬКО ДЛЯ ЧТЕНИЯ — оно
# не запоминает ни серверов, ни входа. То же самое происходит при запуске из
# «Загрузок», и ради этого случая уже заведён отдельный экран в main.cpp.
#
# Ярлык на «Программы» рядом с приложением превращает установку в одно
# перетаскивание и снимает нужду объяснять её словами. Это и есть обычный вид
# установщика на этой системе.
STAGE="$DEST/dmg"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Программы"

DMG="$DEST/GreenRhythm-$version_standalone-macos-$ARCH.dmg"
hdiutil create -volname "GreenRhythm" -srcfolder "$STAGE" -ov -format UDZO "$DMG"
rm -rf "$STAGE"

# ПРОВЕРЯЕМ СВОЙ РЕЗУЛЬТАТ, а не верим ему на слово: образ должен открываться и
# содержать оба предмета. Битый образ снаружи неотличим от целого, и узнать об
# этом от человека — худший из возможных способов.
MNT=$(mktemp -d)
hdiutil attach "$DMG" -nobrowse -readonly -mountpoint "$MNT" >/dev/null
fail() { echo "$1" >&2; hdiutil detach "$MNT" >/dev/null 2>&1; exit 1; }
test -d "$MNT/GreenRhythm.app" || fail "в образе нет приложения"
test -L "$MNT/Программы" || fail "в образе нет ярлыка на Программы"
test -x "$MNT/GreenRhythm.app/Contents/MacOS/greenrhythm_core" || fail "в образе нет исполняемого ядра"
test -f "$MNT/GreenRhythm.app/Contents/MacOS/geoip.db" || fail "в образе нет базы сетевых адресов"
test -f "$MNT/GreenRhythm.app/Contents/Resources/greenrhythm.icns" || fail "в образе нет значка"
hdiutil detach "$MNT" >/dev/null
rmdir "$MNT" 2>/dev/null || true

echo "готово: $DMG"
ls -la "$DEST"
