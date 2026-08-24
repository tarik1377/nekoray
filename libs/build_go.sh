#!/bin/bash
set -e

source libs/env_deploy.sh
[ "$GOOS" == "windows" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/windows64 || true
[ "$GOOS" == "windows" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/windows-arm64 || true
[ "$GOOS" == "linux" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/linux64 || true
[ "$GOOS" == "linux" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/linux-arm64 || true
# Каталог с хвостом «-go» намеренно: сам deploy_macos.sh собирает бандл в
# deployment/macos-<arch> и начинает с rm -rf. Общий каталог означал бы, что
# выкладка стирает бинари, которые ей же и нужны, — причём ровно в том порядке,
# в котором её и запускают.
[ "$GOOS" == "darwin" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/macos-amd64-go || true
[ "$GOOS" == "darwin" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/macos-arm64-go || true
if [ -z $DEST ]; then
  echo "Please set GOOS GOARCH"
  exit 1
fi
rm -rf $DEST
mkdir -p $DEST

export CGO_ENABLED=0

#### Go: updater ####
#
# ПОД DARWIN ОН СОБИРАЕТСЯ ТОЖЕ. Здесь стояло `[ "$GOOS" == "darwin" ] ||` —
# то есть на маке updater просто не собирался. Молчаливой эта дыра не осталась
# бы: main.cpp на всех платформах, кроме Windows, делает
# QFile::link("launcher", "updater"), и без файла симлинк повисает битым, а
# «Обновить» превращается в кнопку, которая ничего не делает и ничего не
# говорит.
pushd go/cmd/updater
go build -o $DEST -trimpath -ldflags "-w -s"
# Имя launcher — не Linux-особенность, а то, чего ждёт main.cpp везде, кроме
# Windows: он заводит symlink updater -> launcher при первом запуске.
#
# Через case, а не цепочкой `[ A ] || [ B ] && mv || true`: у || и && в оболочке
# одинаковый приоритет и левая ассоциативность, и такая цепочка читается совсем
# не так, как выглядит. Ошибка в ней не отказывает — она просто не переименует
# файл, и «Обновить» перестанет работать на одной из платформ.
case "$GOOS" in
  linux | darwin) mv $DEST/updater $DEST/launcher ;;
esac
popd

#### Go: greenrhythm_core ####
pushd go/cmd/nekobox_core
GO_EXT=""
[ "$GOOS" == "windows" ] && GO_EXT=".exe"
# Read the version from go.mod instead of repeating it here. It was hardcoded, so
# bumping the dependency left the binary reporting the old version — the log, the
# support reports and every bug we chased would have said 1.13.5 while running
# something else, and nothing would have flagged the mismatch.
SING_BOX_VERSION=$(grep -m1 'github.com/sagernet/sing-box v' go.mod | sed 's/.*sing-box v//' | tr -d ' \t\r')
if [ -z "$SING_BOX_VERSION" ]; then
  echo "cannot read sing-box version from go.mod"
  exit 1
fi
echo "sing-box version: $SING_BOX_VERSION"
# Quote each -X value: the Go tool splits -ldflags on whitespace, so an unquoted
# version would silently become extra arguments for the linker instead of an error.
go build -v -o $DEST/greenrhythm_core${GO_EXT} -trimpath -ldflags "-w -s -X 'github.com/matsuridayo/libneko/neko_common.Version_neko=$version_standalone' -X 'github.com/sagernet/sing-box/constant.Version=$SING_BOX_VERSION'" -tags "with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api"
popd
