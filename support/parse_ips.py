#!/usr/bin/env python3
"""Достаёт из отчёта macOS о падении то, ради чего его вообще открывают.

ЗАЧЕМ ОТДЕЛЬНЫМ ФАЙЛОМ. Раньше это делалось прямо в шаге сборки, и брало оно
`head -25`. Первый же прогон показал цену: в аннотации уехала одна JSON-шапка —
имя программы, версия системы, uuid — и ни одного кадра. Формат .ips устроен
так: первая строка заголовок, дальше ОДИН json целиком, поэтому резка по
строкам даёт что угодно, кроме места падения.

Переписать это внутри YAML не вышло дважды: тело heredoc обязано стоять на
базовом отступе блока, иначе рвётся либо YAML, либо bash, либо отступ python.
Отдельный файл снимает вопрос совсем и, в отличие от строчки в шаге,
проверяется сам по себе.

Журнал прогона по API отдаётся только тем, у кого есть права на репозиторий,
поэтому найденное печатается как ::error:: — аннотации читаются открыто.

Использование: python3 support/parse_ips.py <файл.ips> [сколько кадров]
Ничего не роняет: диагностика не имеет права уронить шаг, который и так
разбирает отказ.
"""

import json
import sys

LIMIT = 400  # длина строки аннотации; длиннее — нечитаемо и всё равно обрежут


def err(text):
    print("::error::crash: " + str(text)[:LIMIT])


def main():
    if len(sys.argv) < 2:
        return 0
    frames_wanted = int(sys.argv[2]) if len(sys.argv) > 2 else 24

    try:
        with open(sys.argv[1], encoding="utf-8", errors="replace") as fh:
            raw = fh.read()
    except OSError as exc:
        err("не открылся отчёт: %s" % exc)
        return 0

    parts = raw.split("\n", 1)
    if len(parts) < 2:
        err("в отчёте нет тела, только заголовок")
        return 0

    try:
        report = json.loads(parts[1])
    except ValueError as exc:
        err("тело отчёта не разобралось: %s" % exc)
        return 0

    err("exception: %s" % report.get("exception"))
    err("termination: %s" % report.get("termination"))

    images = report.get("usedImages") or []
    threads = report.get("threads") or []
    faulting = report.get("faultingThread")
    err("faultingThread: %s, всего потоков %d" % (faulting, len(threads)))

    if not isinstance(faulting, int) or not 0 <= faulting < len(threads):
        return 0

    frames = (threads[faulting].get("frames") or [])[:frames_wanted]
    for i, frame in enumerate(frames):
        index = frame.get("imageIndex")
        name = "?"
        if isinstance(index, int) and 0 <= index < len(images):
            image = images[index]
            name = image.get("name") or image.get("path") or "?"
        err("  #%-2d %-30s +%s %s" % (i, name, frame.get("imageOffset"),
                                      frame.get("symbol") or ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
