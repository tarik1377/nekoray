/**
 * Проверка разбора вех резервного подключения.
 *
 * Набор существует ради одной строчки — той, что требует конца строки после
 * последнего числа. Именно она не пускает в журнал адрес, приписанный за
 * форматом, и именно её проще всего потерять при следующей правке: без неё всё
 * остальное продолжает работать, а утечка происходит молча.
 *
 * Запуск: ninja relaytrace_test && ./relaytrace_test
 */

#include "main/RelayTrace.hpp"

#include <QCoreApplication>
#include <QString>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static int checks = 0;
static int fails = 0;

/**
 * printf, а не qInfo.
 *
 * qInfo в сборке Release заглушён правилами логирования: набор печатал пустоту
 * и выходил с нулём, то есть выглядел пройденным, ничего не показав. Тест,
 * результат которого нельзя увидеть, — не тест.
 */
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

static void says(const QString &what, const QString &line, const QString &expectPart) {
    const auto got = RelayTrace::Line(line);
    checks++;
    if (got.contains(expectPart)) {
        say("  ок    " + what);
    } else {
        fails++;
        say("  ПЛОХО " + what + " — вышло: " + (got.isEmpty() ? QString("<пусто>") : got));
    }
}

static void silent(const QString &what, const QString &line) {
    const auto got = RelayTrace::Line(line);
    checks++;
    if (got.isEmpty()) {
        say("  ок    " + what);
    } else {
        fails++;
        say("  ПЛОХО " + what + " — пропустило: " + got);
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    say("");
    say("RelayTrace");

    // Движок печатает вехи через log.Printf, то есть с меткой времени впереди.
    const QString stamp = "2026/08/24 01:23:45 ";

    // ---- что должно быть сказано ----

    says("все каналы подтверждены",
         stamp + "relay-milestone warm ok=4 of=4 took=900ms limit=12000ms",
         "подтверждены все за 900 мс");

    says("ни один канал не построен — это про хранилище, а не про узел",
         stamp + "relay-milestone warm ok=0 of=0 took=12000ms limit=12000ms",
         "не поднято ни одного");

    says("построены, но не отозвались, срок не вышел",
         stamp + "relay-milestone warm ok=0 of=4 took=900ms limit=12000ms",
         "не подтверждён ни один за 900 мс");

    says("построены, не отозвались, срок вышел — виноват срок",
         stamp + "relay-milestone warm ok=0 of=4 took=12000ms limit=12000ms",
         "предел 12000 мс вышел");

    says("часть подтверждена, срок не вышел",
         stamp + "relay-milestone warm ok=2 of=4 took=900ms limit=12000ms",
         "подтверждены не все за 900 мс");

    says("часть подтверждена, срок вышел",
         stamp + "relay-milestone warm ok=2 of=4 took=12000ms limit=12000ms",
         "подтверждены не все, предел 12000 мс вышел");

    says("ранняя веха читается как «остальные ещё идут»",
         stamp + "relay-milestone carry took=430ms limit=12000ms",
         "первый подтверждён за 430 мс");

    // ---- о чём надо молчать ----

    // ГЛАВНЫЙ СЛУЧАЙ НАБОРА. Ровно эту строку движок печатает при старте
    // (native-src/main.go), и ровно она рассказывает, как устроен канал.
    silent("строка о транспорте не попадает в журнал",
           stamp + "SOCKS5 ready on 127.0.0.1:1080 (transport: object storage)");

    // ВТОРОЙ ГЛАВНЫЙ. Формат соблюдён, но за ним приписан адрес — если хвост не
    // проверять, он уедет в журнал вместе с разобранной фразой.
    silent("хвост за форматом отвергает строку целиком",
           stamp + "relay-milestone warm ok=4 of=4 took=900ms limit=12000ms host=storage.example.net");

    silent("хвост у ранней вехи — так же",
           stamp + "relay-milestone carry took=430ms limit=12000ms dir=a1b2/0123456789abcdef");

    silent("посторонняя строка", stamp + "listening on some address");
    silent("пустая строка", "");
    silent("только метка без чисел", stamp + "relay-milestone warm ");
    silent("нечисло вместо числа", stamp + "relay-milestone warm ok=x of=4 took=900ms limit=12000ms");
    silent("пропущено поле", stamp + "relay-milestone warm ok=4 took=900ms limit=12000ms");
    silent("подтверждённых больше построенных",
           stamp + "relay-milestone warm ok=9 of=4 took=900ms limit=12000ms");
    silent("отрицательное число", stamp + "relay-milestone warm ok=-1 of=4 took=900ms limit=12000ms");
    silent("слишком длинное число",
           stamp + "relay-milestone warm ok=4 of=4 took=9999999999999999999999ms limit=12000ms");

    // ---- из строки не переносится ни одного символа ----

    {
        const QString dirty = stamp + "relay-milestone warm ok=4 of=4 took=900ms limit=12000ms";
        const auto said = RelayTrace::Line(dirty);
        is("во фразе нет метки формата", !said.contains("relay-milestone"));
        is("во фразе нет латиницы из строки движка", !said.contains("ok=") && !said.contains("took="));
        is("во фразе нет метки времени", !said.contains("2026/08/24"));
    }

    say("");
    say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
    std::fflush(stdout);
    return fails > 0 ? 1 : 0;
}
