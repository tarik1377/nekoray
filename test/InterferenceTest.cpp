/**
 * Помехи: разведка, приостановка и — главное — возврат.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Здесь единственное место в программе, где мы своими
 * руками выключаем ЧУЖУЮ работающую программу. Ошибка тут не роняет клиент и
 * не портит подключение: она оставляет человека без рабочего VPN, без доступа к
 * корпоративной сети или без туннеля до дома — и он даже не свяжет это с нами,
 * потому что мы к тому моменту уже закрылись.
 *
 * Проверять это на живой машине нечем: каждая проверка означала бы выключить
 * себе сеть и посмотреть, вернётся ли она. Поэтому построение скриптов сделано
 * чистым, а набор читает сами скрипты и сторожит порядок действий в них.
 *
 * ТРИ ИНВАРИАНТА, РАДИ КОТОРЫХ ВСЁ И ЗАТЕВАЛОСЬ.
 *
 *  1. СНИМОК ЛОЖИТСЯ НА ДИСК ДО ПЕРВОЙ ОСТАНОВКИ. Обратный порядок выглядит
 *     естественнее и читается лучше — и стоил бы человеку рабочего VPN ровно в
 *     тот момент, когда мы упали между двумя действиями.
 *
 *  2. СНИМОК УДАЛЯЕТСЯ ПОСЛЕ ПОСЛЕДНЕГО ВОЗВРАТА. Удали его в начале — и
 *     прерывание на середине оставит половину служб выключенными, а записи о
 *     том, что их надо вернуть, уже не будет.
 *
 *  3. СЛУЖБА ПЕРЕВОДИТСЯ В «ВРУЧНУЮ», А НЕ В «ОТКЛЮЧЕНО». Отключённую человек
 *     не вернёт одним движением из окна свойств, если наш возврат почему-то не
 *     сработает. «Вручную» оставляет ему дверь.
 *
 * Запуск: ninja interference_test && ./interference_test
 */

#include "main/Interference.hpp"

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

/** Настоящая форма вывода разведки: поля через табуляцию. */
static QString sampleScan() {
    const QChar t('\t');
    QStringList lines;
    lines << QStringLiteral("service") + t + QStringLiteral("CloudflareWARP") + t + QStringLiteral("Cloudflare WARP")
                 + t + QStringLiteral("Running, запуск Auto") + t + QStringLiteral("1") + t + QStringLiteral("1") + t
                 + QString();
    lines << QStringLiteral("service") + t + QStringLiteral("CheckPointEndpoint") + t
                 + QStringLiteral("Check Point Endpoint Security") + t + QStringLiteral("Running, запуск Auto") + t
                 + QStringLiteral("1") + t + QStringLiteral("1") + t
                 + QStringLiteral("корпоративный клиент: без него может пропасть доступ к рабочей сети");
    lines << QStringLiteral("adapter") + t + QStringLiteral("WARP") + t + QStringLiteral("Cloudflare WARP Adapter") + t
                 + QStringLiteral("Up") + t + QStringLiteral("1") + t + QStringLiteral("1") + t
                 + QStringLiteral("забирает весь трафик: у него маршрут по умолчанию");
    lines << QStringLiteral("process") + t + QStringLiteral("winws") + t + QStringLiteral("winws") + t
                 + QStringLiteral("работает") + t + QStringLiteral("1") + t + QStringLiteral("0") + t
                 + QStringLiteral("запущено вручную: вернуть сможете только вы сами");
    lines << QStringLiteral("мусор без полей");
    lines << QString();
    return lines.join(QChar('\n'));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // ── Разбор разведки ──────────────────────────────────────────────────
    std::fputs("Разбор разведки\n", stdout);
    const auto found = parseScan(sampleScan());
    is(QStringLiteral("разобраны все полные строки, мусор отброшен"), found.size() == 4);
    if (found.size() == 4) {
        is(QStringLiteral("служба опознана"), found[0].kind == Meddler::Service
                                                  && found[0].key == QStringLiteral("CloudflareWARP"));
        is(QStringLiteral("человеческое имя взято из описания"),
           found[0].title == QStringLiteral("Cloudflare WARP"));
        is(QStringLiteral("состояние «работает» прочитано"), found[0].running);
        is(QStringLiteral("обратимость прочитана"), found[0].reversible);
        is(QStringLiteral("пустой риск остаётся пустым"), found[0].risk.isEmpty());

        // Корпоративный клиент обязан приезжать с предупреждением: человек,
        // выключивший его «чтобы игра заработала», теряет доступ к работе.
        is(QStringLiteral("корпоративный клиент приезжает с предупреждением"),
           found[1].risk.contains(QStringLiteral("рабочей сети")));

        is(QStringLiteral("адаптер опознан"), found[2].kind == Meddler::Adapter);
        is(QStringLiteral("маршрут по умолчанию назван прямо"),
           found[2].risk.contains(QStringLiteral("весь трафик")));

        // Правило 3: чего не умеем вернуть — то без галочки.
        is(QStringLiteral("процесс помечен как НЕобратимый"),
           found[3].kind == Meddler::Process && !found[3].reversible);
    }
    is(QStringLiteral("пустой ввод даёт пустой список, а не одну пустую строку"),
       parseScan(QString()).isEmpty());

    // ── Разведка не трогает своё ─────────────────────────────────────────
    std::fputs("\nРазведка\n", stdout);
    {
        const auto s = scanScript();
        is(QStringLiteral("свой каталог исключается по пути"), s.contains(QStringLiteral("$own")));
        is(QStringLiteral("наш туннель в список помех не попадает"),
           s.contains(QStringLiteral("neko-tun")) && s.contains(QStringLiteral("sing-tun")));
        // Разведка обязана быть безобидной: покажи мы список ценой остановки
        // чего-нибудь — и «посмотреть, что мешает» стало бы действием.
        is(QStringLiteral("разведка ничего не останавливает и не удаляет"),
           !s.contains(QStringLiteral("Stop-Service")) && !s.contains(QStringLiteral("Remove-Item"))
               && !s.contains(QStringLiteral("Disable-NetAdapter")) && !s.contains(QStringLiteral("sc.exe")));
        is(QStringLiteral("железные адаптеры не предлагаются"), s.contains(QStringLiteral("HardwareInterface")));
    }

    // ── Приостановка ─────────────────────────────────────────────────────
    std::fputs("\nПриостановка\n", stdout);
    {
        const auto script = pauseScript(found, QStringLiteral("C:/p/interference-paused.tsv"));

        // ИНВАРИАНТ 1. Запись снимка обязана стоять раньше любой остановки.
        const int write = script.indexOf(QStringLiteral("Set-Content"));
        const int stopSvc = script.indexOf(QStringLiteral("Stop-Service"));
        const int stopAda = script.indexOf(QStringLiteral("Disable-NetAdapter"));
        is(QStringLiteral("снимок пишется до остановки службы"), write >= 0 && stopSvc > write);
        is(QStringLiteral("снимок пишется до отключения адаптера"), write >= 0 && stopAda > write);

        // ИНВАРИАНТ 3. «Вручную», а не «отключено».
        is(QStringLiteral("служба переводится в «вручную»"), script.contains(QStringLiteral("start= demand")));
        is(QStringLiteral("служба НЕ отключается насовсем"), !script.contains(QStringLiteral("start= disabled")));

        // «Починить сеть» удаляет; здесь — никогда.
        is(QStringLiteral("ничего не удаляется: ни служба, ни драйвер"),
           !script.contains(QStringLiteral("sc.exe delete")) && !script.contains(QStringLiteral("Unregister")));

        is(QStringLiteral("выбранная служба попала в скрипт"),
           script.contains(QStringLiteral("'CloudflareWARP'")));
        is(QStringLiteral("выбранный адаптер попал в скрипт"), script.contains(QStringLiteral("'WARP'")));

        // Правило 3 ещё раз, теперь на выходе: необратимое не трогаем даже
        // тогда, когда человек его выбрал.
        is(QStringLiteral("необратимый процесс не убивается"),
           !script.contains(QStringLiteral("Stop-Process")) && !script.contains(QStringLiteral("winws")));
    }
    {
        const auto empty = pauseScript({}, QStringLiteral("C:/p/s.tsv"));
        is(QStringLiteral("на пустом выборе скрипт ничего не останавливает"),
           !empty.contains(QStringLiteral("Stop-Service")) && !empty.contains(QStringLiteral("Disable-NetAdapter")));
    }
    {
        // Имя службы с апострофом — не выдумка: адаптеры зовут «Ethernet 2»,
        // а описания бывают с кавычками. Незакрытая кавычка в PowerShell
        // означает не отказ, а выполнение чего-то другого.
        Meddling m;
        m.kind = Meddler::Service;
        m.key = QStringLiteral("Bob's VPN");
        m.reversible = true;
        const auto s = pauseScript({m}, QStringLiteral("C:/p/s.tsv"));
        is(QStringLiteral("апостроф в имени удваивается"), s.contains(QStringLiteral("'Bob''s VPN'")));
    }

    // ── Возврат ──────────────────────────────────────────────────────────
    std::fputs("\nВозврат\n", stdout);
    {
        const auto script = resumeScript(QStringLiteral("C:/p/interference-paused.tsv"));

        // ИНВАРИАНТ 2. Снимок удаляется последним.
        const int remove = script.indexOf(QStringLiteral("Remove-Item"));
        const int restore = script.lastIndexOf(QStringLiteral("sc.exe config"));
        const int start = script.lastIndexOf(QStringLiteral("Start-Service"));
        is(QStringLiteral("снимок удаляется после возврата служб"), remove > restore);
        is(QStringLiteral("снимок удаляется после запуска служб"), remove > start);

        is(QStringLiteral("возвращается прежний тип запуска, все пять видов"),
           script.contains(QStringLiteral("start= auto")) && script.contains(QStringLiteral("start= demand"))
               && script.contains(QStringLiteral("start= disabled")) && script.contains(QStringLiteral("start= boot"))
               && script.contains(QStringLiteral("start= system")));
        is(QStringLiteral("служба запускается обратно, только если работала"),
           script.contains(QStringLiteral("$state -eq 'Running'")));
        is(QStringLiteral("адаптер включается обратно"), script.contains(QStringLiteral("Enable-NetAdapter")));
        is(QStringLiteral("путь снимка подставлен"), script.contains(QStringLiteral("'C:/p/interference-paused.tsv'")));
        is(QStringLiteral("возврат ничего не останавливает"),
           !script.contains(QStringLiteral("Stop-Service")) && !script.contains(QStringLiteral("Disable-NetAdapter")));
    }

    // ── Снимок словами ───────────────────────────────────────────────────
    std::fputs("\nСнимок словами\n", stdout);
    {
        const QChar t('\t');
        const QString snap = QStringLiteral("service") + t + QStringLiteral("CloudflareWARP") + t
                             + QStringLiteral("Auto") + t + QStringLiteral("Running") + QChar('\n')
                             + QStringLiteral("adapter") + t + QStringLiteral("WARP") + t + QStringLiteral("Up") + t
                             + QStringLiteral("Up");
        const auto said = snapshotSummary(snap);
        is(QStringLiteral("обе строки прочитаны"), said.size() == 2);
        is(QStringLiteral("служба названа службой, состояние показано"),
           !said.isEmpty() && said[0].contains(QStringLiteral("служба CloudflareWARP"))
               && said[0].contains(QStringLiteral("Auto")));
        is(QStringLiteral("адаптер назван адаптером"),
           said.size() > 1 && said[1].contains(QStringLiteral("адаптер WARP")));
        is(QStringLiteral("пустой снимок — пустой список"), snapshotSummary(QString()).isEmpty());
    }

    std::fputs((QStringLiteral("\nПроверок: %1, провалов: %2\n").arg(checks).arg(fails)).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
