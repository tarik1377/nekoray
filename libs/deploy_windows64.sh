#!/bin/bash
set -e

source libs/env_deploy.sh
DEST=$DEPLOYMENT/windows64
rm -rf $DEST
mkdir -p $DEST

#### copy exe ####
cp $BUILD/nekobox.exe $DEST

#### deploy qt & DLL runtime ####
pushd $DEST
windeployqt nekobox.exe --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw --verbose 2
rm -rf translations
rm -rf libEGL.dll libGLESv2.dll Qt6Pdf.dll

if [ "$DL_QT_VER" != "5.15" ]; then
  cp $SRC_ROOT/qtsdk/Qt/bin/libcrypto-3-x64.dll .
  cp $SRC_ROOT/qtsdk/Qt/bin/libssl-3-x64.dll .
fi

popd

#### copy icon ####
cp $SRC_ROOT/res/public/greenrhythm.png $DEST/greenrhythm.png

#### copy default config templates ####
mkdir -p $DEST/config/routes_box
mkdir -p $DEST/config/groups
cp $SRC_ROOT/res/config_template/routes_box/Default $DEST/config/routes_box/Default
cp $SRC_ROOT/res/config_template/groups/nekobox.json $DEST/config/groups/nekobox.json

#### copy .srs rule-set files into config/ (sing-box CWD = config/) ####
cp $DEST/../public_res/geosite-category-ads-all.srs $DEST/config/ 2>/dev/null || true
cp $DEST/../public_res/geoip-ru.srs $DEST/config/ 2>/dev/null || true

#### prepare deployment ####
cp $BUILD/*.pdb $DEPLOYMENT 2>/dev/null || true
