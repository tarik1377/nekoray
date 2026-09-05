/**
 * Подписи строки в таблице соединений.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Таблица соединений — то, куда человек смотрит, когда
 * что-то не работает. Три строки «— · Напрямую · 185.194.32.150» на снимке
 * владельца читались как неизвестные программы мимо VPN, а были нашим же
 * туннелем к серверу. Подпись, которая врёт в такой момент, хуже пустой.
 *
 * Образцы — с того же снимка, 05.09.2026.
 *
 * Запуск: ninja connection_row_test && ./connection_row_test
 */

#include "main/ConnectionRow.hpp"

#include <QCoreApplication>

#include <cstdio>

using namespace GreenRhythm;

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

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    std::fputs("Хост без порта\n", stdout);
    is(QStringLiteral("IPv4 с портом"), hostWithoutPort(QStringLiteral("185.194.32.150:1193")) == QStringLiteral("185.194.32.150"));
    is(QStringLiteral("имя с портом"), hostWithoutPort(QStringLiteral("a.claude.ai:443")) == QStringLiteral("a.claude.ai"));
    is(QStringLiteral("без порта — как есть"), hostWithoutPort(QStringLiteral("a.claude.ai")) == QStringLiteral("a.claude.ai"));
    is(QStringLiteral("IPv6 в скобках"), hostWithoutPort(QStringLiteral("[2a02:6b8::1]:443")) == QStringLiteral("2a02:6b8::1"));
    is(QStringLiteral("голый IPv6 не режется"), hostWithoutPort(QStringLiteral("2a02:6b8::1")) == QStringLiteral("2a02:6b8::1"));
    is(QStringLiteral("хвост не число — не порт"), hostWithoutPort(QStringLiteral("host:abc")) == QStringLiteral("host:abc"));

    std::fputs("\nНазначение\n", stdout);
    is(QStringLiteral("имя впереди, адрес хвостом, порт один раз"),
       destinationLabel(QStringLiteral("149.154.167.41:443"), QStringLiteral("a.claude.ai:443"))
           == QStringLiteral("a.claude.ai:443  ·  149.154.167.41"));
    is(QStringLiteral("без имени — адрес с портом как есть"),
       destinationLabel(QStringLiteral("95.183.11.208:21116"), QString()) == QStringLiteral("95.183.11.208:21116"));
    is(QStringLiteral("имя совпало с адресом — не дублируется"),
       destinationLabel(QStringLiteral("1.2.3.4:443"), QStringLiteral("1.2.3.4:443")) == QStringLiteral("1.2.3.4:443"));
    is(QStringLiteral("пробелы по краям не мешают"),
       destinationLabel(QStringLiteral(" 1.2.3.4:443 "), QStringLiteral(" x.y:443 ")) == QStringLiteral("x.y:443  ·  1.2.3.4"));

    std::fputs("\nПрограмма\n", stdout);
    is(QStringLiteral("имя процесса — как есть"),
       programLabel(QStringLiteral("Telegram.exe"), QStringLiteral("149.154.167.41:443"), QStringLiteral("185.194.32.150"))
           == QStringLiteral("Telegram.exe"));
    // Вот та самая строка со снимка.
    is(QStringLiteral("без процесса к серверу профиля — это наш туннель"),
       programLabel(QString(), QStringLiteral("185.194.32.150:1193"), QStringLiteral("185.194.32.150")) == tunnelLabel());
    is(QStringLiteral("сервер задан именем — сравнение без учёта регистра"),
       programLabel(QString(), QStringLiteral("Orsana.Adshkola.ru:443"), QStringLiteral("orsana.adshkola.ru")) == tunnelLabel());
    is(QStringLiteral("без процесса к чужому адресу — прочерк"),
       programLabel(QString(), QStringLiteral("8.8.8.8:53"), QStringLiteral("185.194.32.150")) == QStringLiteral("—"));
    is(QStringLiteral("сервер не задан — прочерк, а не туннель"),
       programLabel(QString(), QStringLiteral("185.194.32.150:1193"), QString()) == QStringLiteral("—"));
    is(QStringLiteral("подпись туннеля не пуста"), !tunnelLabel().isEmpty());

    std::fputs((QStringLiteral("\nПроверок: %1, провалов: %2\n").arg(checks).arg(fails)).toUtf8().constData(), stdout);
    return fails == 0 ? 0 : 1;
}
