#!/bin/bash
#
# Готовит сборку ядра с правками sing-box и sing-vmess и печатает имя go.mod,
# который надо передать в -modfile.
#
#   go build -modfile "$(bash _overlay/prepare.sh)" ...
#
# ЗАЧЕМ. Ядру нужны правки REALITY и выбора интерфейса Windows (patches/README.md). Форк
# всего sing-box в репозитории ради нескольких файлов — это чужая история и чужие
# обновления на нашей совести. Правка в кэше модулей не годится: он сверяется
# по контрольным суммам.
#
# ПОЧЕМУ НЕ -overlay. Он был первым выбором — подменить один файл для
# компилятора, — но Go отказывает прямо: «Files beneath GOMODCACHE must not be
# replaced». Поэтому здесь копия модуля: sing-box той же версии копируется из
# кэша в _overlay/build/, туда кладутся правленые файлы, а рядом с go.mod
# пишется go.overlay.mod с replace на эту копию. Закоммиченный go.mod не
# меняется, кэш не трогается.
#
# go.overlay.mod лежит в корне модуля, а не в _overlay/: относительные replace
# из go.mod (grpc_server, libneko) должны значить то же самое, что и там.
#
# Каталог начинается с подчёркивания, и это не украшение: go ./... пропускает
# такие каталоги, поэтому ни копия sing-box, ни правленые файлы не собираются
# как пакеты нашего модуля.
#
# Зовётся из go/cmd/nekobox_core.
set -e

patched="_overlay/sing-box/common/tls/reality_client.go"
if [ ! -f "$patched" ]; then
  echo "prepare.sh: запускать из go/cmd/nekobox_core, файла $patched не видно" >&2
  exit 1
fi

# ВЕРСИЯ СВЕРЯЕТСЯ. Правленый файл — копия файла конкретной версии sing-box.
# Поднимут версию в go.mod, не переснимая копию, — в сборку ушёл бы старый
# файл поверх нового пакета. Лучше отказ сборки с понятной причиной.
want=$(grep -m1 '^// overlay-base: github.com/sagernet/sing-box ' "$patched" | awk '{print $4}')
have=$(grep -m1 'github.com/sagernet/sing-box v' go.mod | sed 's/.*sing-box //' | tr -d ' \t\r')
if [ -z "$want" ] || [ "$want" != "$have" ]; then
  echo "prepare.sh: копия клиента REALITY снята с sing-box ${want:-?}, а в go.mod $have." >&2
  echo "prepare.sh: перенесите правку на новую версию (patches/README.md) и обновите строку overlay-base." >&2
  exit 1
fi

# Every copied source has a base-version header, including Windows route helpers.
while IFS= read -r file; do
  base=$(grep -m1 '^// overlay-base: github.com/sagernet/sing-box ' "$file" | awk '{print $4}')
  if [ "$base" != "$have" ]; then
    echo "prepare.sh: $file is based on ${base:-?}, expected $have" >&2
    exit 1
  fi
done < <(find _overlay/sing-box -type f -name '*.go')

# Vision must parse stream boundaries independently of outer TLS records.
# Pin the second module independently: updating sing-box must not silently
# keep a Vision source copied from an incompatible sing-vmess version.
vmess_have=$(grep -m1 'github.com/sagernet/sing-vmess v' go.mod | sed 's/.*sing-vmess //' | awk '{print $1}' | tr -d '\r')
if [ ! -f _overlay/sing-vmess/vless/vision.go ] || [ -z "$vmess_have" ]; then
  echo "prepare.sh: pinned sing-vmess Vision overlay or module version missing" >&2
  exit 1
fi
while IFS= read -r file; do
  base=$(grep -m1 '^// overlay-base: github.com/sagernet/sing-vmess ' "$file" | awk '{print $4}')
  if [ "$base" != "$vmess_have" ]; then
    echo "prepare.sh: $file is based on ${base:-?}, expected $vmess_have" >&2
    exit 1
  fi
done < <(find _overlay/sing-vmess -type f -name '*.go')

# СНАЧАЛА СКАЧАТЬ, ПОТОМ СПРАШИВАТЬ. go list -m модуль не качает: на чистой
# машине — а раннер CI каждый раз чистый — .Dir пришёл бы пустым, и скрипт
# отказал бы на ровном месте. go mod download ничего не делает, если модуль уже
# в кэше, и сверяет его по go.sum, если качает.
go mod download github.com/sagernet/sing-box
src=$(go list -m -f '{{.Dir}}' github.com/sagernet/sing-box)
src="${src//\\//}"
if [ ! -f "$src/go.mod" ]; then
  echo "prepare.sh: модуль sing-box не найден в кэше и после go mod download ($src)" >&2
  exit 1
fi

dst="_overlay/build/sing-box"
rm -rf "$dst"
mkdir -p "_overlay/build"
cp -r "$src" "$dst"
# Кэш модулей только для чтения, и копия наследует это.
chmod -R u+w "$dst"
while IFS= read -r file; do
  relative="${file#_overlay/sing-box/}"
  mkdir -p "$dst/$(dirname "$relative")"
  cp "$file" "$dst/$relative"
done < <(find _overlay/sing-box -type f -name '*.go')

# Путь в replace — абсолютный и в виде, понятном Go на этой машине: из Git Bash
# на Windows pwd дал бы /c/..., которого Go не знает.
abs="$(cd "$dst" && (pwd -W 2>/dev/null || pwd))"

cp go.mod go.overlay.mod
cp go.sum go.overlay.sum
go mod edit -replace "github.com/sagernet/sing-box=$abs" go.overlay.mod

go mod download github.com/sagernet/sing-vmess
vmess_src=$(go list -m -f '{{.Dir}}' github.com/sagernet/sing-vmess)
vmess_src="${vmess_src//\\//}"
if [ ! -f "$vmess_src/go.mod" ]; then
  echo "prepare.sh: sing-vmess module not found after download ($vmess_src)" >&2
  exit 1
fi
vmess_dst="_overlay/build/sing-vmess"
rm -rf "$vmess_dst"
cp -r "$vmess_src" "$vmess_dst"
chmod -R u+w "$vmess_dst"
while IFS= read -r file; do
  relative="${file#_overlay/sing-vmess/}"
  mkdir -p "$vmess_dst/$(dirname "$relative")"
  cp "$file" "$vmess_dst/$relative"
done < <(find _overlay/sing-vmess -type f -name '*.go')
vmess_abs="$(cd "$vmess_dst" && (pwd -W 2>/dev/null || pwd))"
go mod edit -replace "github.com/sagernet/sing-vmess=$vmess_abs" go.overlay.mod

echo "go.overlay.mod"
