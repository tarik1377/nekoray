/**
 * Исключения из туннеля не смеют накрывать наши же поддельные адреса.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Две постоянные в двух файлах обязаны сходиться, и они
 * разошлись молча: из туннеля исключался fc00::/7 — «все локальные адреса
 * шестой версии», — а поддельный диапазон раздавался из fc00::/18, то есть
 * ИЗНУТРИ исключённого. Каждый выданный поддельный адрес объявлялся домашней
 * сетью и выводился наружу, а знать о нём, кроме туннеля, некому: имя,
 * разрешённое в такой адрес, переставало открываться совсем.
 *
 * Отказ был тихим вдвойне. Он проявляется только при включённом IPv6 в
 * туннеле (по умолчанию выключен), и выглядит как «часть сайтов не грузится» —
 * то есть как беда сети, а не как две строки, глядящие друг на друга.
 *
 * Сторож читает ОБА файла и проверяет отношение между ними, а не переписанные
 * сюда значения: переписанное — третья копия той же постоянной, и разойтись она
 * может ровно так же, только теперь молча и с зелёными проверками.
 *
 * Запуск: ninja tun_exclude_test && ./tun_exclude_test
 */

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static int checks = 0;
static int fails = 0;

static void say(const QString &s) {
    std::fputs(s.toUtf8().constData(), stdout);
    std::fputc('\n', stdout);
}

static void is(const QString &what, bool ok) {
    checks++;
    if (ok) {
        say("  ок    " + what);
    } else {
        fails++;
        say("  ПЛОХО " + what);
    }
}

/** Файл ищется от корня дерева: набор запускают из разных каталогов. */
static QString readTree(const QString &relative) {
    for (const auto &prefix: {QString(""), QString("../"), QString("../../")}) {
        QFile f(prefix + relative);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll());
    }
    return {};
}

/** Накрывает ли префикс-исключение весь названный диапазон. */
static bool covers(const QString &excludePrefix, const QString &rangePrefix) {
    const auto exclude = QHostAddress::parseSubnet(excludePrefix);
    const auto range = QHostAddress::parseSubnet(rangePrefix);
    if (exclude.first.isNull() || range.first.isNull()) return false;
    // Разные семейства не пересекаются по определению.
    if (exclude.first.protocol() != range.first.protocol()) return false;
    // Накрывает, если начало диапазона внутри исключения И исключение шире:
    // более узкое исключение внутри нашего диапазона отняло бы только часть,
    // и это тоже беда, поэтому проверяется вхождение начала, а не длина.
    return range.first.isInSubnet(exclude);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    say("");
    say("Исключения из туннеля против собственных поддельных диапазонов");

    const auto tpl = readTree("res/vpn/sing-box-vpn.json");
    const auto builder = readTree("db/ConfigBuilder.cpp");

    is("шаблон внешнего туннеля прочитан", !tpl.isEmpty());
    is("построитель конфигурации прочитан", !builder.isEmpty());
    if (tpl.isEmpty() || builder.isEmpty()) {
        say("");
        say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
        return 1;
    }

    // Поддельные диапазоны — те, что мы раздаём сами.
    QStringList fake;
    const QRegularExpression fakeRe(R"RX("inet[46]_range"\s*:\s*"([^"]+)")RX");
    for (const auto &text: {tpl, builder}) {
        auto it = fakeRe.globalMatch(text);
        while (it.hasNext()) fake << it.next().captured(1);
    }
    fake.removeDuplicates();
    is("поддельные диапазоны найдены в обоих файлах", fake.size() >= 2);
    say("        раздаём: " + fake.join(", "));

    // Исключения — из обоих источников: шаблона и построителя.
    QStringList excludes;
    {
        // В шаблоне это массив route_exclude_address; берём весь блок и из него
        // все строковые значения, похожие на префикс.
        const int at = tpl.indexOf("route_exclude_address");
        if (at >= 0) {
            const int end = tpl.indexOf("]", at);
            const auto block = tpl.mid(at, end - at);
            const QRegularExpression pfx(R"RX("([0-9a-fA-F:.]+/\d+)")RX");
            auto it = pfx.globalMatch(block);
            while (it.hasNext()) excludes << it.next().captured(1);
        }
    }
    {
        // В построителе это литеральный список рядом с "10.0.0.0/8".
        const int at = builder.indexOf("\"10.0.0.0/8\"");
        if (at >= 0) {
            const auto block = builder.mid(at, 400);
            const QRegularExpression pfx(R"RX("([0-9a-fA-F:.]+/\d+)")RX");
            auto it = pfx.globalMatch(block);
            while (it.hasNext()) excludes << it.next().captured(1);
        }
    }
    excludes.removeDuplicates();
    is("исключения найдены в обоих источниках", excludes.size() >= 6);
    say("        исключаем: " + excludes.join(", "));

    // ГЛАВНАЯ ПРОВЕРКА, ради которой всё и заведено.
    for (const auto &range: fake) {
        for (const auto &exclude: excludes) {
            const bool bad = covers(exclude, range);
            if (bad) {
                is(QString("исключение %1 накрывает наш диапазон %2 — "
                           "поддельные адреса уйдут мимо туннеля в никуда")
                       .arg(exclude, range),
                   false);
            }
        }
    }
    is("ни одно исключение не накрывает наш поддельный диапазон", fails == 0);

    say("");
    say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
    std::fflush(stdout);
    return fails > 0 ? 1 : 0;
}
