/**
 * Значки: каждое имя из кода существует и вписано в ресурсы.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Icons::pixmap читает SVG из ресурса по имени. Нет
 * файла — QFile не откроется, помощник вернёт прозрачный квадрат, и кнопка
 * останется без значка. Ни ошибки, ни предупреждения: пункт «Маршруты» просто
 * будет с пустотой слева, и заметит это человек на снимке или в окне, а не
 * сборка. Опечатка в одном имени из шестнадцати — ровно такой случай.
 *
 * Проверяются три вещи по исходникам:
 *   — каждое имя gr-* из кода есть файлом в res/icon;
 *   — каждый такой файл вписан в res/neko.qrc (иначе в бинарь не попадёт);
 *   — каждый файл — линия цвета currentColor: иначе окраска в Icons не
 *     сработает и значок выйдет чёрным на тёмном.
 *
 * Запуск: ninja icons_test && ./icons_test
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <cstdio>

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok    ") : QStringLiteral("  ПРОВАЛ ")) + what
                + QStringLiteral("\n"))
                   .toUtf8()
                   .constData(),
               stdout);
}

static QString root() {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        if (QFile::exists(prefix + QStringLiteral("res/neko.qrc"))) return prefix;
    }
    return {};
}

static QString slurp(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString r = root();
    is(QStringLiteral("корень дерева найден"), !r.isEmpty());

    // Имена из кода: везде, где значки зовут по имени.
    QSet<QString> used;
    const QRegularExpression name(QStringLiteral("\"(gr-[a-z0-9-]+)\""));
    for (const auto &src: {QStringLiteral("ui/MainShell.cpp"), QStringLiteral("ui/LogAndConnections.cpp"),
                           QStringLiteral("test/MainWindowPreview.cpp")}) {
        const auto text = slurp(r + src);
        is(QStringLiteral("исходник прочитан: %1").arg(src), !text.isEmpty());
        auto it = name.globalMatch(text);
        while (it.hasNext()) used << it.next().captured(1);
    }
    is(QStringLiteral("в коде есть хотя бы десяток имён значков (найдено %1)").arg(used.size()),
       used.size() >= 10);

    const auto qrc = slurp(r + QStringLiteral("res/neko.qrc"));
    is(QStringLiteral("neko.qrc прочитан"), !qrc.isEmpty());

    std::fputs("\nКаждое имя из кода\n", stdout);
    QStringList sorted = used.values();
    sorted.sort();
    for (const auto &n: sorted) {
        const QString file = r + QStringLiteral("res/icon/%1.svg").arg(n);
        const bool exists = QFile::exists(file);
        is(QStringLiteral("%1 — файл есть").arg(n), exists);
        is(QStringLiteral("%1 — вписан в neko.qrc").arg(n),
           qrc.contains(QStringLiteral("icon/%1.svg").arg(n)));
        if (!exists) continue;
        const auto svg = slurp(file);
        is(QStringLiteral("%1 — рисуется currentColor").arg(n), svg.contains(QStringLiteral("currentColor")));
        is(QStringLiteral("%1 — сетка 24×24").arg(n), svg.contains(QStringLiteral("viewBox=\"0 0 24 24\"")));
    }

    std::fputs((QStringLiteral("\nПроверок: %1, провалов: %2\n").arg(checks).arg(fails)).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
