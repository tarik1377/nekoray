/**
 * Разбор «что случилось с программой».
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Приговор выносится по счётчикам, а счётчики легко
 * соврать дважды: посчитать одно долгое соединение тридцать раз (ядро присылает
 * его в каждом опросе) или назвать «поломкой» намеренный порядок. Оба вранья
 * заканчиваются одинаково — человек нажимает «Починить», ничего не меняется, и
 * доверия к механизму больше нет.
 *
 * Отдельно закреплено то, чего механизм НЕ умеет: запрещённые соединения до
 * учёта не доходят, поэтому приговора «программу не пускают» не существует.
 * Набор обязан падать, если кто-то решит такой приговор добавить.
 *
 * Запуск: ninja program_trouble_test && ./program_trouble_test
 */

#include "main/ProgramTrouble.hpp"

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

static Seen conn(const QString &proc, const QString &tag, const QString &net,
                 const QString &dest, qint64 start) {
    Seen s;
    s.process = proc;
    s.tag = tag;
    s.network = net;
    s.dest = dest;
    s.start = start;
    return s;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QString game = QStringLiteral("SquadGame-Win64-Shipping.exe");

    // ---- повтор одного соединения не должен множиться ----
    {
        Watch w;
        const auto one = conn(game, "proxy", "udp", "185.207.214.36:15020", 1000);
        for (int i = 0; i < 30; i++) w.add({one}); // тридцать опросов, соединение одно
        const auto f = w.finish(game);
        is(QStringLiteral("одно соединение осталось одним, а не тридцатью"), f.viaTunnel == 1);
        is(QStringLiteral("замечено ровно одно"), w.total() == 1);
    }

    // ---- живой случай Squad: HTTPS мимо туннеля, UDP к серверу — в туннель ----
    {
        Watch w;
        w.add({conn(game, "bypass", "tcp", "104.16.0.1:443", 1),
               conn(game, "bypass", "tcp", "104.16.0.2:443", 2),
               conn(game, "proxy", "udp", "185.207.214.36:15020", 3)});
        const auto f = w.finish(game);
        is(QStringLiteral("смешанный случай опознан"), f.verdict == Verdict::Mixed);
        is(QStringLiteral("UDP в туннель посчитан отдельно"), f.udpViaTunnel == 1);
        // Ровно эта пара чисел и отличает сегодняшний случай от «всё в порядке»:
        // по одному лишь «часть ходит мимо» вывод был бы «жалоб нет».
        is(QStringLiteral("мимо туннеля тоже посчитано"), f.direct == 2);
    }

    // ---- всё через туннель ----
    {
        Watch w;
        w.add({conn(game, "proxy", "udp", "1.2.3.4:15020", 1),
               conn(game, "proxy", "tcp", "1.2.3.5:443", 2)});
        is(QStringLiteral("«ходит через туннель» опознано"),
           w.finish(game).verdict == Verdict::ThroughTunnel);
    }

    // ---- программа молчала ----
    {
        Watch w;
        w.add({conn("chrome.exe", "proxy", "tcp", "1.1.1.1:443", 1)});
        const auto f = w.finish(game);
        is(QStringLiteral("молчавшая программа не осуждена"), f.verdict == Verdict::NotSeen);
        // И тут же — подсказка про соседа: человек мог назвать лаунчер вместо игры.
        is(QStringLiteral("сосед по туннелю назван"),
           f.companions.size() == 1 && f.companions.first() == QStringLiteral("chrome.exe"));
    }

    // ---- и так ходит напрямую ----
    {
        Watch w;
        w.add({conn(game, "bypass", "udp", "1.2.3.4:15020", 1)});
        const auto f = w.finish(game);
        is(QStringLiteral("«и так напрямую» опознано"), f.verdict == Verdict::Direct);
        is(QStringLiteral("сам себе в соседи не попал"), f.companions.isEmpty());
    }

    // ---- имя сравнивается без учёта регистра ----
    {
        Watch w;
        w.add({conn(QStringLiteral("squadgame-win64-shipping.EXE"), "proxy", "udp", "1.2.3.4:1", 1)});
        is(QStringLiteral("регистр имени не мешает"),
           w.finish(game).verdict == Verdict::ThroughTunnel);
    }

    // ---- запрещённых соединений в учёте нет и быть не может ----
    {
        Watch w;
        // Даже если такая запись каким-то образом придёт, приговором она не станет:
        // до учёта запрет в ядре не доходит, и делать вид, что мы его видим, нельзя.
        w.add({conn(game, "block", "udp", "1.2.3.4:443", 1)});
        const auto f = w.finish(game);
        is(QStringLiteral("запрет не выдаётся за наблюдение"), f.verdict == Verdict::NotSeen);
        is(QStringLiteral("запрет не посчитан ни туда, ни сюда"),
           f.viaTunnel == 0 && f.direct == 0);
    }

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
