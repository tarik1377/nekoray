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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

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

    // ---- ПЕРЕХВАТ ИМЁН ВЫШЕ ВСЕГО, ЧТО ЗАДАЁТ ЧЕЛОВЕК ----
    //
    // Разрешение имён идёт на адрес внутри туннеля. Пользовательский список
    // «мимо туннеля», обогнавший перехват, уводит эти запросы в никуда: человек
    // вписывает 172.16.0.0/12, чтобы не заводить в туннель рабочую сеть, и
    // остаётся без разрешения имён вовсе. Связать это со своей же строкой в
    // настройках он не сможет никогда — поэтому отношение закреплено здесь.
    const int dnsAdd   = src.indexOf(QStringLiteral("status->routingRulesDns +="));
    const int dnsFirst = src.indexOf(QStringLiteral("auto routingRules = status->routingRulesDns"));
    const int userNext = src.indexOf(QStringLiteral("QJSONARRAY_ADD(routingRules, status->routingRulesFirst)"));

    is(QStringLiteral("перехват имён живёт отдельным массивом"), dnsAdd >= 0);
    is(QStringLiteral("цепочка начинается с перехвата имён"), dnsFirst >= 0);
    is(QStringLiteral("поимённый список человека приклеивается ПОСЛЕ перехвата"),
       dnsFirst >= 0 && userNext > dnsFirst);

    // ---- ВТОРОЕ ОТНОШЕНИЕ: обход по процессу выше блокировок ----
    //
    // Тот же закон «первое совпадение решает», но в цепочке правил из пресета.
    // Список игр «мимо туннеля» стоял ПОСЛЕ блокировки udp/443, и потому не
    // работал для игр, которым этот порт нужен: пакет гасился раньше, чем
    // доходил до списка. Так пропал список серверов в Squad — он берёт его
    // через Epic Online Services, а те ходят по udp/443. Снаружи это выглядит
    // как «клиент не видит серверов», и обход в настройках не помогает.
    const QString gui = slurp(QStringLiteral("main/NekoGui.cpp"));
    is(QStringLiteral("пресет маршрутизации прочитан"), !gui.isEmpty());

    const int at = gui.indexOf(QStringLiteral("custom = \"{"));
    is(QStringLiteral("цепочка правил найдена в пресете"), at >= 0);
    if (at >= 0) {
        QString line = gui.mid(at, gui.indexOf(QChar('\n'), at) - at);
        line = line.mid(line.indexOf(QChar('"')) + 1);
        line = line.left(line.lastIndexOf(QChar('"')));
        // Снимаем экранирование C++ по-настоящему, а не вычёркиванием слэшей:
        // в цепочке есть шаблон пути, где обратный слэш ЗНАЧАЩИЙ, и грубая
        // чистка молча превратила бы "\." в "." — регулярное выражение осталось
        // бы рабочим на вид и разъехалось бы по смыслу.
        QString json;
        for (int i = 0; i < line.size(); i++) {
            if (line[i] == QChar('\\') && i + 1 < line.size()) {
                json += line[++i];
            } else {
                json += line[i];
            }
        }
        line = json;

        const auto doc = QJsonDocument::fromJson(line.toUtf8());
        const auto rules = doc.object().value(QStringLiteral("rules")).toArray();
        is(QStringLiteral("цепочка разобралась"), !rules.isEmpty());

        int bypassAt = -1;
        int blockAt = -1;
        bool squad = false;
        bool squadExact = false;
        bool epicBypassed = false;
        int regexAt = -1;
        int icmpAt = -1;
        QStringList pathPatterns;
        for (int i = 0; i < rules.size(); i++) {
            const auto o = rules[i].toObject();
            const auto out = o.value(QStringLiteral("outbound")).toString();
            if (bypassAt < 0 && out == QStringLiteral("bypass")
                && o.contains(QStringLiteral("process_name"))) {
                bypassAt = i;
                for (const auto &n: o.value(QStringLiteral("process_name")).toArray()) {
                    const auto nm = n.toString();
                    if (nm.startsWith(QStringLiteral("SquadGame"))) squad = true;
                    if (nm == QStringLiteral("SquadGame-Win64-Shipping.exe")) squadExact = true;
                    if (nm.startsWith(QStringLiteral("Epic"))) epicBypassed = true;
                }
            }
            if (regexAt < 0 && out == QStringLiteral("bypass")
                && o.contains(QStringLiteral("process_path_regex"))) {
                regexAt = i;
                for (const auto &p: o.value(QStringLiteral("process_path_regex")).toArray()) {
                    pathPatterns += p.toString();
                }
            }
            if (icmpAt < 0 && out == QStringLiteral("bypass")
                && o.value(QStringLiteral("network")).toString() == QStringLiteral("icmp")) {
                icmpAt = i;
            }
            if (blockAt < 0 && out == QStringLiteral("block")) blockAt = i;
        }

        is(QStringLiteral("правило «мимо туннеля» по процессу есть"), bypassAt >= 0);
        is(QStringLiteral("блокировка в цепочке есть"), blockAt >= 0);
        // ГЛАВНОЕ ОТНОШЕНИЕ.
        is(QStringLiteral("обход по процессу идёт ДО блокировок"),
           bypassAt >= 0 && blockAt > bypassAt);
        is(QStringLiteral("Squad в списке обхода"), squad);
        is(QStringLiteral("Squad записан настоящим именем сборки"), squadExact);
        // Лаунчеру Epic напрямую нельзя: его сеть не пускает с прямого адреса, и
        // он отвечает «ошибка соединения». Один раз он в этот список уже попал —
        // как часть списка «с запасом», собранного по догадке.
        is(QStringLiteral("лаунчера Epic в обходе нет"), !epicBypassed);
        // Имя игры на Unreal нельзя знать заранее, поэтому рядом с поимённым
        // списком стоит шаблон по суффиксу сборки — он и ловит будущие игры.
        is(QStringLiteral("есть правило по шаблону пути"), regexAt >= 0);
        is(QStringLiteral("шаблон тоже идёт ДО блокировок"),
           regexAt >= 0 && blockAt > regexAt);
        // Поля внутри одного правила складываются по «И» (rule_abstract.go:127):
        // имя и шаблон в одном правиле не совпали бы одновременно никогда.
        is(QStringLiteral("имя и шаблон стоят РАЗНЫМИ правилами"),
           regexAt >= 0 && bypassAt >= 0 && regexAt != bypassAt);

        // ---- ШАБЛОН ПРОВЕРЯЕТСЯ РАБОТОЙ, А НЕ НАЛИЧИЕМ ----
        //
        // Наличие шаблона ничего не обещает: прежний ловил только сам файл игры
        // (-Win64-Shipping) и пропускал ВТОРОЙ исполняемый файл, который Unreal
        // кладёт рядом, — лаунчер игры. У Wardogs это WardogsLauncher-Shipping.exe:
        // суффикса платформы в имени нет, под шаблон он не подходил и уходил в
        // туннель, пока сама игра шла мимо. Игра предъявляла один адрес, её лаунчер
        // разговаривал с сервером античита с другого — и это вечный экран загрузки
        // при полностью исправной сети.
        //
        // Поэтому здесь проверяется совпадение на настоящих путях. Список нарочно
        // держит и старые случаи: перейдя на один общий суффикс, легко потерять то,
        // ради чего заводились два прежних шаблона.
        auto matchesAny = [&pathPatterns](const QString &path) {
            for (const auto &p: pathPatterns) {
                if (QRegularExpression(p).match(path).hasMatch()) return true;
            }
            return false;
        };

        is(QStringLiteral("шаблон вообще есть, что проверять"), !pathPatterns.isEmpty());
        is(QStringLiteral("ловит сам файл игры (-Win64-Shipping)"),
           matchesAny(QStringLiteral(R"(F:\SteamLibrary\steamapps\common\WARDOGS Playtest\Wardogs\Binaries\Win64\WardogsClient-Win64-Shipping.exe)")));
        is(QStringLiteral("ловит ЛАУНЧЕР игры (-Shipping без платформы)"),
           matchesAny(QStringLiteral(R"(F:\SteamLibrary\steamapps\common\WARDOGS Playtest\WardogsLauncher-Shipping.exe)")));
        is(QStringLiteral("прежний случай Squad не потерян"),
           matchesAny(QStringLiteral(R"(D:\Steam\steamapps\common\Squad\SquadGame\Binaries\Win64\SquadGame-Win64-Shipping.exe)")));
        is(QStringLiteral("прежний случай сборки под Microsoft Store не потерян"),
           matchesAny(QStringLiteral(R"(C:\XboxGames\Sea of Thieves\Content\SoTGame-WinGDK-Shipping.exe)")));
        // Обратная сторона: шаблон обязан оставаться узким. «Мимо туннеля» — это
        // отказ от защиты, и раздавать его чему попало нельзя.
        is(QStringLiteral("не ловит системные программы"),
           !matchesAny(QStringLiteral(R"(C:\Windows\System32\svchost.exe)")));
        is(QStringLiteral("не ловит браузер"),
           !matchesAny(QStringLiteral(R"(C:\Program Files\Mozilla Firefox\firefox.exe)")));
        is(QStringLiteral("не ловит наше собственное ядро"),
           !matchesAny(QStringLiteral(R"(C:\GreenRhythm\greenrhythm_core.exe)")));

        // ICMP ловится ТОЛЬКО правилом по сети. У него нет порта, поэтому ядро не
        // определяет процесс (ProcessPath пуст), и правила по имени и по шаблону
        // отказывают первой же строкой, до сравнения. Пинг в браузере серверов
        // идёт именно по ICMP — без этого правила он показывает прочерк, сколько
        // имён процессов в список ни впиши.
        is(QStringLiteral("есть правило «icmp — мимо туннеля»"), icmpAt >= 0);
        is(QStringLiteral("оно идёт ДО блокировок"), icmpAt >= 0 && blockAt > icmpAt);
    }

    // ---- ПРАВКА ПРЕСЕТА БЕЗ СВОЕГО ФЛАГА НЕ ДОХОДИТ НИ ДО КОГО ----
    //
    // Сохранённая схема заслоняет пресет навсегда, а проход по схемам ходит ровно
    // один раз за флаг. Это ловушка с историей: из-за неё блокировка QUIC не
    // досталась никому, кто уже открывал клиент, и понадобился второй флаг для
    // порядка правил. Расширенный шаблон пути — третий такой случай.
    //
    // Флаг обязан быть заведён В ТРЁХ местах сразу, и половина связки хуже, чем
    // ничего: объявлен, но не записан — проход побежит при каждом запуске;
    // записан, но не прочитан — не побежит никогда. Отношение и проверяется.
    const QString store = slurp(QStringLiteral("main/NekoGui_DataStore.hpp"));
    const QString win   = slurp(QStringLiteral("ui/mainwindow.cpp"));
    const QString flag  = QStringLiteral("routing_launcher_migrated");
    is(QStringLiteral("объявление настроек прочитано"), !store.isEmpty());
    is(QStringLiteral("окно прочитано"), !win.isEmpty());
    is(QStringLiteral("флаг ремонта объявлен"), store.contains(flag));
    is(QStringLiteral("флаг ремонта сохраняется в файл настроек"),
       gui.contains(QStringLiteral("configItem(\"") + flag));
    is(QStringLiteral("флаг ремонта читается при запуске"), win.contains(flag));

    // ---- ОБРАТНОЕ НАПРАВЛЕНИЕ И ИГРЫ ЧЕРЕЗ ТУННЕЛЬ ----
    //
    // Список человека умел только выводить из-под защиты. Обратный список и
    // переключатель «игры через туннель» обязаны стоять в routingRulesFirst —
    // раньше пресета и списков доменов, иначе шаблон «-Shipping» из пресета
    // заберёт игру первым и переключатель не сделает ничего, оставаясь на вид
    // включённым. И ПОСЛЕ поимённого обхода человека: сказанное «мимо»
    // поимённо решает раньше общего «через».
    const int bypassUser = src.indexOf(QStringLiteral("QString process_name_rule = dataStore->vpn_rule_process.trimmed();"));
    const int proxyUser  = src.indexOf(QStringLiteral("dataStore->vpn_rule_process_proxy.trimmed()"));
    const int gamesFlip  = src.indexOf(QStringLiteral("if (dataStore->games_via_tunnel)"));
    is(QStringLiteral("обратный список программ собирается"), proxyUser >= 0);
    is(QStringLiteral("разворот игровых правил собирается"), gamesFlip >= 0);
    is(QStringLiteral("обратный список идёт ПОСЛЕ поимённого обхода"),
       bypassUser >= 0 && proxyUser > bypassUser);
    is(QStringLiteral("разворот игр идёт ПОСЛЕ обратного списка"),
       proxyUser >= 0 && gamesFlip > proxyUser);
    is(QStringLiteral("обратный список кладётся в routingRulesFirst"),
       proxyUser >= 0 && src.mid(proxyUser, 400).contains(QStringLiteral("routingRulesFirst")));
    is(QStringLiteral("разворот кладётся в routingRulesFirst"),
       gamesFlip >= 0 && src.mid(gamesFlip, 700).contains(QStringLiteral("routingRulesFirst")));
    // Разворачивать можно только правила ПО ПРОЦЕССУ: перевёрнутая блокировка
    // QUIC стала бы правилом «QUIC — в туннель», ровно наоборот к замыслу.
    is(QStringLiteral("разворот отбирает только правила по процессу"),
       gamesFlip >= 0 && src.mid(gamesFlip, 700).contains(QStringLiteral("process_path_regex")));
    is(QStringLiteral("обратный список сохраняется в настройки"),
       gui.contains(QStringLiteral("configItem(\"vpn_proxy_process\"")));
    is(QStringLiteral("переключатель игр сохраняется в настройки"),
       gui.contains(QStringLiteral("configItem(\"games_via_tunnel\"")));

    // ---- ПОРТ CLASH API — СВОЙ, А НЕ СОСЕДНИЙ ----
    //
    // core_port + 1 никто не проверял на занятость; ядро падало на старте с
    // «Only one usage of each socket address». Порт обязан выбираться так же,
    // как core_port, и подставляться в конфиг.
    is(QStringLiteral("порт Clash API выбирается при запуске"),
       win.contains(QStringLiteral("core_clash_port = p")));
    is(QStringLiteral("порт Clash API подставляется в конфиг"),
       src.contains(QStringLiteral("dataStore->core_clash_port > 0 ? dataStore->core_clash_port")));

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
