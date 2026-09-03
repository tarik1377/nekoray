/**
 * Разбор состояния портов машины.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Беда, которую он ловит, невидима: Windows отдаёт
 * номера портов Hyper-V, WSL и Docker, а программа, попросившая такой номер,
 * получает отказ доступа и жалуется на что угодно, только не на порт. На живой
 * машине владельца динамический диапазон был расширен до 1024-65534 вместо
 * штатных 49152-65535, система нарезала в нём 27 блоков, и один накрыл девять
 * фиксированных портов игрового сервиса. Тот падал без объяснений, лаунчер
 * писал «ошибка соединения», установка игры срывалась — и всё это выглядело
 * поломкой сети. Поиск занял полночи.
 *
 * Образцы вывода здесь — НАСТОЯЩИЕ, снятые с той машины. Проверка, написанная
 * по выдуманному образцу, проверяет собственную выдумку: разбор развалится о
 * первую же живую строчку, а набор останется зелёным.
 *
 * Запуск: ninja port_health_test && ./port_health_test
 */

#include "main/PortHealth.hpp"

#include <QCoreApplication>

#include <cstdio>

using namespace GreenRhythm;

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

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // Настоящий вывод с машины владельца: диапазон расширен.
    const QString widened = QStringLiteral(
        "\nProtocol tcp Dynamic Port Range\n"
        "---------------------------------\n"
        "Start Port      : 1024\n"
        "Number of Ports : 64511\n");

    // Штатная машина.
    const QString normal = QStringLiteral(
        "\nProtocol tcp Dynamic Port Range\n"
        "---------------------------------\n"
        "Start Port      : 49152\n"
        "Number of Ports : 16384\n");

    const QString excluded = QStringLiteral(
        "\nProtocol tcp Port Exclusion Ranges\n"
        "\n"
        "Start Port    End Port\n"
        "----------    --------\n"
        "     35767       35866\n"
        "     50000       50059     *\n"
        "     62015       62114\n");

    {
        const auto h = parsePortHealth(widened, excluded);
        is(QStringLiteral("начало диапазона прочитано"), h.dynamicFrom == 1024);
        is(QStringLiteral("размер диапазона прочитан"), h.dynamicCount == 64511);
        is(QStringLiteral("расширение замечено"), h.dynamicRangeWidened());
        is(QStringLiteral("все три запрета разобраны"), h.reserved.size() == 3);
        is(QStringLiteral("занятых номеров посчитано верно"), h.reservedPorts() == 100 + 60 + 100);

        // Ровно те порты, на которых всё и сломалось.
        is(QStringLiteral("порт игрового сервиса опознан занятым"), h.isReserved(35783));
        is(QStringLiteral("наш порт из живого отказа опознан занятым"), h.isReserved(62060));
        is(QStringLiteral("свободный порт занятым не считается"), !h.isReserved(40000));

        is(QStringLiteral("есть о чём говорить человеку"), h.worthReporting());
        const auto v = h.verdict();
        is(QStringLiteral("приговор не пуст"), !v.isEmpty());
        // Приговор обязан назвать причину, а не только число: без неё человек
        // прочтёт «занято 260 портов» и не поймёт, при чём тут его игра.
        is(QStringLiteral("приговор объясняет причину"), v.contains(QStringLiteral("расширен")));
        is(QStringLiteral("приговор даёт лечение"), v.contains(QStringLiteral("dynamicport")));
        // Строчка со звёздочкой — заявка приложения, но занят есть занят.
        is(QStringLiteral("строка со звёздочкой тоже разобрана"), h.isReserved(50030));
    }

    {
        const auto h = parsePortHealth(normal, excluded);
        is(QStringLiteral("штатный диапазон расширением не считается"), !h.dynamicRangeWidened());
        // 260 занятых номеров при штатном диапазоне — обычная жизнь машины с
        // Hyper-V, и пугать ими незачем.
        is(QStringLiteral("при штатном диапазоне и малом резерве молчим"), !h.worthReporting());
        is(QStringLiteral("и приговор пуст"), h.verdict().isEmpty());
    }

    {
        // Пустой вывод — система не ответила. Это НЕ «всё хорошо»: молчим, но и
        // не выдумываем ничего.
        const auto h = parsePortHealth(QString(), QString());
        is(QStringLiteral("на пустом выводе ничего не выдумано"),
           h.reserved.isEmpty() && h.dynamicCount == 0);
    }

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
