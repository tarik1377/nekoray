#!/bin/sh
set -e
set -x

if [ "$(id -u)" -ne 0 ]; then
  echo "[Warning] Tun script not running as root"
fi

# КАВЫЧКИ ВОКРУГ ПУТИ ОБЯЗАТЕЛЬНЫ. На macOS каталог настроек — это
# ~/Library/Application Support/…, то есть путь с пробелом. Без кавычек cd
# уходит в "Application", падает по set -e, и человек видит, что туннель
# включился и сразу выключился, — без единого внятного сообщения.
BASEDIR=$(dirname "$0")
cd "$BASEDIR"

# Правила iptables — ЛИНУКСОВЫЕ, и только линуксовые. На macOS их нет вовсе:
# команда не находится, set -e останавливает скрипт, и ядро не запускается
# никогда. Тот же признак нужен и в stop, иначе выход из туннеля падал бы на
# том же месте, не сняв за собой ничего.
is_linux() {
  [ "$(uname -s)" = "Linux" ]
}

pre_start_linux() {
  is_linux || return 0
  command -v iptables >/dev/null 2>&1 || return 0
  # for Tun2Socket
  iptables -I INPUT -s 172.19.0.2 -d 172.19.0.1 -p tcp -j ACCEPT
  ip6tables -I INPUT -s fdfe:dcba:9876::2 -d fdfe:dcba:9876::1 -p tcp -j ACCEPT
}

start() {
  pre_start_linux
  "./greenrhythm_core" run -c "$CONFIG_PATH"
}

stop() {
  is_linux || return 0
  command -v iptables >/dev/null 2>&1 || return 0
  iptables -D INPUT -s 172.19.0.2 -d 172.19.0.1 -p tcp -j ACCEPT
  ip6tables -D INPUT -s fdfe:dcba:9876::2 -d fdfe:dcba:9876::1 -p tcp -j ACCEPT
}

# ОТКАЗ СТАРТА БОЛЬШЕ НЕ ГЛУШИТСЯ. Здесь стояло `start || true`, и это значило,
# что не поднявшийся туннель выглядел поднявшимся: скрипт выходил с нулём,
# приложение считало включение удавшимся, а человек оставался без туннеля и без
# единого слова о том, почему.
#
# У stop `|| true` остаётся намеренно: не сумев прибрать за собой, выходить с
# ошибкой незачем — прибирать больше нечего, а отказ на выходе выглядел бы как
# новая поломка.
if [ "$1" != "stop" ]; then
  start
fi

stop || true
