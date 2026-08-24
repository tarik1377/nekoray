#!/bin/bash
set -e

cd libs

# 参数
if [ -z $cmake ]; then
  cmake="cmake"
fi

# СОВМЕСТИМОСТЬ С CMAKE 4.
#
# Здесь собираются zxing-cpp 2.0.0, yaml-cpp 0.7.0 и protobuf 21.4 — проекты
# нескольких лет от роду, и часть из них объявляет cmake_minimum_required ниже
# 3.5. CMake 4 считает это не устареванием, а ОШИБКОЙ («Compatibility with CMake
# < 3.5 has been removed») и обрывает конфигурацию.
#
# Ловится это не сразу: на Windows каталог libs/deps лежит в кэше CI, и пока кэш
# цел, зависимости не пересобираются вовсе. Поломка проявится в тот день, когда
# кэш промахнётся, — и будет выглядеть как «сборка вдруг перестала идти».
#
# CMAKE_POLICY_VERSION_MINIMUM заведён в CMake 3.31 ровно для такого случая. На
# версиях старше это просто неизвестная переменная: CMake напишет, что она не
# использована, и продолжит. То есть строка безопасна везде.
POLICY_MIN="-DCMAKE_POLICY_VERSION_MINIMUM=3.5"

# ЦЕЛЕВАЯ ВЕРСИЯ MACOS — ТА ЖЕ, ЧТО У ПРИЛОЖЕНИЯ.
#
# Без этого зависимости собираются под версию сборочной машины (раннеры — macOS
# 15), а приложение объявляет 12.0. Компоновщик про это предупреждает — «object
# file was built for newer macOS version than being linked», — но собирает, и
# получается бинарь, который ОБЕЩАЕТ работать на 12 и на ней падает. Обещание,
# которого никто не проверял, хуже честного ограничения.
#
# Значение обязано совпадать с cmake/macos/macos.cmake и LSMinimumSystemVersion.
# На остальных платформах переменная пуста и ни на что не влияет.
MACOS_TARGET=""
if [ "$(uname -s)" = "Darwin" ]; then
  MACOS_TARGET="-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0"
fi
if [ -z $deps ]; then
  deps="deps"
fi

# libs/deps/...
mkdir -p $deps
cd $deps
if [ -z $NKR_PACKAGE ]; then
  INSTALL_PREFIX=$PWD/built
else
  INSTALL_PREFIX=$PWD/package
fi
rm -rf $INSTALL_PREFIX
mkdir -p $INSTALL_PREFIX

#### clean ####
clean() {
  rm -rf dl.zip yaml-* zxing-* protobuf
}

#### ZXing v2.0.0 ####
curl -L -o dl.zip https://github.com/nu-book/zxing-cpp/archive/refs/tags/v2.0.0.zip
unzip dl.zip

cd zxing-*
mkdir -p build
cd build

$cmake .. -GNinja $POLICY_MIN $MACOS_TARGET -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_BLACKBOX_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
ninja && ninja install

cd ../..

#### yaml-cpp ####
curl -L -o dl.zip https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.7.0.zip
unzip dl.zip

cd yaml-*
mkdir -p build
cd build

$cmake .. -GNinja $POLICY_MIN $MACOS_TARGET -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
ninja && ninja install

cd ../..

#### protobuf ####
git clone --recurse-submodules -b v21.4 --depth 1 --shallow-submodules https://github.com/protocolbuffers/protobuf

#备注：交叉编译要在 host 也安装 protobuf 并且版本一致,编译安装，同参数，安装到 /usr/local

mkdir -p protobuf/build
cd protobuf/build

$cmake .. -GNinja $POLICY_MIN $MACOS_TARGET \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -Dprotobuf_MSVC_STATIC_RUNTIME=OFF \
  -Dprotobuf_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
ninja && ninja install

cd ../..

####
clean
