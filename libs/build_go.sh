#!/bin/bash
set -e

source libs/env_deploy.sh
[ "$GOOS" == "windows" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/windows64 || true
[ "$GOOS" == "windows" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/windows-arm64 || true
[ "$GOOS" == "linux" ] && [ "$GOARCH" == "amd64" ] && DEST=$DEPLOYMENT/linux64 || true
[ "$GOOS" == "linux" ] && [ "$GOARCH" == "arm64" ] && DEST=$DEPLOYMENT/linux-arm64 || true
if [ -z $DEST ]; then
  echo "Please set GOOS GOARCH"
  exit 1
fi
rm -rf $DEST
mkdir -p $DEST

export CGO_ENABLED=0

#### Go: updater ####
pushd go/cmd/updater
[ "$GOOS" == "darwin" ] || go build -o $DEST -trimpath -ldflags "-w -s"
[ "$GOOS" == "linux" ] && mv $DEST/updater $DEST/launcher || true
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
go build -v -o $DEST/greenrhythm_core${GO_EXT} -trimpath -ldflags "-w -s -X github.com/matsuridayo/libneko/neko_common.Version_neko=$version_standalone -X github.com/sagernet/sing-box/constant.Version=$SING_BOX_VERSION" -tags "with_clash_api,with_gvisor,with_quic,with_wireguard,with_utls,with_v2ray_api"
popd
