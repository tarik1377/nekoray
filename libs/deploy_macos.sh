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

#### значок и шаблоны настроек ####
cp "$SRC_ROOT/res/public/greenrhythm.png" "$BIN/greenrhythm.png"
mkdir -p "$BIN/config/groups"
cp "$SRC_ROOT/res/config_template/groups/nekobox.json" "$BIN/config/groups/nekobox.json"

#### наборы правил ####
cp "$DEPLOYMENT/public_res/geosite-category-ads-all.srs" "$BIN/config/" 2>/dev/null || true
cp "$DEPLOYMENT/public_res/geoip-ru.srs" "$BIN/config/" 2>/dev/null || true

#### Qt ####
# ПОСЛЕ копирования всех бинарей: macdeployqt правит пути к библиотекам у того,
# что нашёл внутри бандла. Запущенный раньше, он не увидит ни ядро, ни xray, и
# приложение соберётся внешне целым, а упадёт при запуске у человека.
macdeployqt "$APP" -verbose=2

#### образ диска ####
# Простой образ без оформления: без подписи всё равно придётся объяснять
# человеку первый запуск словами, и красивый фон этой задачи не решает.
hdiutil create -volname "GreenRhythm" -srcfolder "$APP" -ov -format UDZO \
  "$DEST/GreenRhythm-$version_standalone-macos-$ARCH.dmg"

echo "готово: $DEST"
