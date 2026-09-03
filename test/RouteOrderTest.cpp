/**
 * Явное исключение обязано стоять ПЕРЕД блоком.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Правила маршрутизации складываются по порядку, и
 * первое совпавшее решает. Блок стоял первым, а значит исключения из него
 * сделать было нельзя вообще: что ни впиши в «Напрямую», до этого правила дело
 * не доходило.
 *
 * Живой случай, с которого всё началось: в «Блок» вписан
 * geosite:category-ads-all, а рекламный кабинет VK живёт на ads.vk.ru — он в
 * той же категории. Кабинет не открывался, и выглядело это как поломка сети:
 * браузер писал «соединение прервано», сайт при этом был жив.
 *
 * Отказ такого рода тихий вдвойне. Он выглядит бедой на той стороне, а не своей
 * настройкой, и человек идёт чинить провайдера. Поэтому порядок закреплён
 * набором: перестановка этих строк ничего не сломает при сборке и не уронит ни
 * одну проверку — она просто вернёт неисправимый блок.
 *
 * Проверяется отношение между строками, а не переписанные сюда номера: номера —
 * третья копия того же знания, и разъезжаются они так же, только молча.
 *
 * Запуск: ninja route_order_test && ./route_order_test
 */

#include <QCoreApplication>
#include <QFile>
#include <QString>

#include <cstdio>

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok    ") : QStringLiteral("  ПРОВАЛ "))
                + what + QStringLiteral("\n"))
                   .toUtf8()
                   .constData(),
               stdout);
}

static QString slurp(const QString &path) {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile f(prefix + path);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll());
    }
    return {};
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QString src = slurp(QStringLiteral("db/ConfigBuilder.cpp"));
    is(QStringLiteral("сборщик конфига прочитан"), !src.isEmpty());
    if (src.isEmpty()) {
        std::fputs("не нашёл db/ConfigBuilder.cpp — запускать из дерева сборки\n", stdout);
        return 1;
    }

    // Точное совпадение отбирается отдельно и выносится вперёд.
    const int picker = src.indexOf(QStringLiteral("only_exact"));
    is(QStringLiteral("отбор точных совпадений на месте"), picker >= 0);

    const int exactDirect = src.indexOf(QStringLiteral("add_rule_route(only_exact(status->domainListDirect)"));
    const int exactRemote = src.indexOf(QStringLiteral("add_rule_route(only_exact(status->domainListRemote)"));
    const int block       = src.indexOf(QStringLiteral("add_rule_route(status->domainListBlock"));
    const int direct      = src.indexOf(QStringLiteral("add_rule_route(status->domainListDirect"));

    is(QStringLiteral("исключение «напрямую» выносится отдельным правилом"), exactDirect >= 0);
    is(QStringLiteral("исключение «через прокси» выносится отдельным правилом"), exactRemote >= 0);
    is(QStringLiteral("блок доменов на месте"), block >= 0);

    // ГЛАВНОЕ ОТНОШЕНИЕ.
    is(QStringLiteral("точное «напрямую» идёт ДО блока"),
       exactDirect >= 0 && block > exactDirect);
    is(QStringLiteral("точное «через прокси» идёт ДО блока"),
       exactRemote >= 0 && block > exactRemote);

    // А широкие списки остались после блока: иначе domain:ru в «Напрямую»
    // снял бы блокировку рекламы со всей российской зоны.
    is(QStringLiteral("широкое «напрямую» осталось ПОСЛЕ блока"),
       direct > block);

    // Точное совпадение — это full:. Другого признака «человек назвал поимённо»
    // в списке нет: domain: покрывает поддомены, geosite: — тысячи чужих имён.
    is(QStringLiteral("признаком точности служит full:"),
       src.mid(picker, 400).contains(QStringLiteral("full:")));

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
