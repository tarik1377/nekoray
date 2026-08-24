#!/bin/bash
set -e

source libs/env_deploy.sh
DEST=$DEPLOYMENT/windows64
rm -rf $DEST
mkdir -p $DEST

#### copy exe ####
cp $BUILD/greenrhythm.exe $DEST

#### deploy qt & DLL runtime ####
pushd $DEST
windeployqt greenrhythm.exe --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw --verbose 2
rm -rf translations
rm -rf libEGL.dll libGLESv2.dll Qt6Pdf.dll

if [ "$DL_QT_VER" != "5.15" ]; then
  cp $SRC_ROOT/qtsdk/Qt/bin/libcrypto-3-x64.dll .
  cp $SRC_ROOT/qtsdk/Qt/bin/libssl-3-x64.dll .
fi

popd

#### лицензии ####
# GPL-3.0 §4 обязывает передавать копию лицензии ВМЕСТЕ с программой, а не
# только держать её в репозитории. Плюс уведомления по чужим компонентам: Qt
# (LGPL), OpenSSL (Apache-2.0), xray (MPL-2.0) — у каждого своё требование, и
# невыполненное обязательство не становится меньше оттого, что о нём не знали.
cp $SRC_ROOT/LICENSE $DEST/LICENSE.txt
cp $SRC_ROOT/THIRD-PARTY-NOTICES.md $DEST/

#### copy icon + wintun driver ####
cp $SRC_ROOT/res/public/greenrhythm.png $DEST/greenrhythm.png
cp $SRC_ROOT/res/public/wintun.dll $DEST/wintun.dll

#### copy default config templates ####
# Ship as little as possible here. A shipped file is loaded verbatim and silently wins over
# every in-code default, so each key duplicated in a template is a default that stops
# tracking the source: the routing template still carried dns_final_out=proxy and no QUIC
# rule long after both were fixed in code. The routing file is gone entirely — the app now
# writes it from the in-code preset on first run — and the settings file keeps only the one
# key that is deliberately different from the header default.
mkdir -p $DEST/config/routes_box
mkdir -p $DEST/config/groups
cp $SRC_ROOT/res/config_template/groups/nekobox.json $DEST/config/groups/nekobox.json

#### copy .srs rule-set files into config/ (sing-box CWD = config/) ####
cp $DEST/../public_res/geosite-category-ads-all.srs $DEST/config/ 2>/dev/null || true
cp $DEST/../public_res/geoip-ru.srs $DEST/config/ 2>/dev/null || true

#### prepare deployment ####
cp $BUILD/*.pdb $DEPLOYMENT 2>/dev/null || true
