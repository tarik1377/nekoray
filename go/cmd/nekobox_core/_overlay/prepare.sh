#!/bin/bash
#
# Готовит сборку ядра с правленым клиентом REALITY и печатает имя go.mod,
# который надо передать в -modfile.
#
#   go build -modfile "$(bash _overlay/prepare.sh)" ...
#
# ЗАЧЕМ. Ядру нужна правка одного файла sing-box (patches/README.md). Форк
# всего sing-box в репозитории ради одного файла — это чужая история и чужие
# обновления на нашей совести. Правка в кэше модулей не годится: он сверяется
# по контрольным суммам.
#
# ПОЧЕМУ НЕ -overlay. Он был первым выбором — подменить один файл для
# компилятора, — но Go отказывает прямо: «Files beneath GOMODCACHE must not be
# replaced». Поэтому здесь копия модуля: sing-box той же версии копируется из
# кэша в _overlay/build/, туда кладётся правленый файл, а рядом с go.mod
# пишется go.overlay.mod с replace на эту копию. Закоммиченный go.mod не
# меняется, кэш не трогается.
#
# go.overlay.mod лежит в корне модуля, а не в _overlay/: относительные replace
# из go.mod (grpc_server, libneko) должны значить то же самое, что и там.
#
# Каталог начинается с подчёркивания, и это не украшение: go ./... пропускает
# такие каталоги, поэтому ни копия sing-box, ни правленый файл не собираются
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
cp "$patched" "$dst/common/tls/reality_client.go"

# Путь в replace — абсолютный и в виде, понятном Go на этой машине: из Git Bash
# на Windows pwd дал бы /c/..., которого Go не знает.
abs="$(cd "$dst" && (pwd -W 2>/dev/null || pwd))"

cp go.mod go.overlay.mod
cp go.sum go.overlay.sum
go mod edit -replace "github.com/sagernet/sing-box=$abs" go.overlay.mod

echo "go.overlay.mod"
