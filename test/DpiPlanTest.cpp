/**
 * План запуска winws: аргументы и содержимое списков.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Аргументы отсюда уезжают в процесс, который поднимает
 * кернел-драйвер и правит чужие пакеты. Ошибка здесь не выглядит ошибкой: она
 * выглядит как «обход включён». Три ловушки, каждая из которых уже стоила
 * времени, и ни одну не видно в работающем клиенте.
 *
 *  1. ПУСТОЙ СПИСОК У ZAPRET ЗНАЧИТ «НЕТ СПИСКА», то есть «дурить всех». Не
 *     «ничего не делать», как читается. Одна пустая строка вместо файла — и
 *     точечный модуль портит рукопожатие каждому соединению машины, включая
 *     банк и рабочую почту. Поэтому в каждом списке стоит сторожевая строка, и
 *     набор проверяет, что ни один файл не пуст.
 *
 *  2. --wf-raw-part СОЕДИНЯЕТСЯ С ПОРТОВЫМ КОНСТРУКТОРОМ ЧЕРЕЗ «ИЛИ». Это
 *     измерено: wf_make_filter из nfq/nfqws.c 72.13 собран отдельно и запущен.
 *     Написав туда «исключить туннель», мы бы не исключили ничего, а ДОБАВИЛИ
 *     ветку, забирающую в пользовательский режим весь трафик вне туннеля.
 *     Ограничение адаптера делает только --wf-iface — он даёт «ifIdx=N and» на
 *     верхнем уровне, перед всей группой.
 *
 *  3. HOSTLIST И IPSET ВНУТРИ ОДНОГО ПРОФИЛЯ СКЛАДЫВАЮТСЯ ПО «И». Нужно «или»:
 *     защищаем и то, что узнали по имени, и то, что знаем по адресу. Значит,
 *     разные профили через --new. Слепив их в один, мы получили бы модуль,
 *     который молча не срабатывает нигде.
 *
 * Запуск: ninja dpi_plan_test && ./dpi_plan_test
 */

#include "dpi/DpiPlan.hpp"

#include <QCoreApplication>
#include <QFile>

#include <cstdio>

using namespace GreenRhythm::Dpi;

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

static QString slurp(const QString &path) {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        QFile f(prefix + path);
        if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll());
    }
    return {};
}

/** Индекс аргумента, иначе -1. */
static int at(const QStringList &a, const QString &v) { return a.indexOf(v); }

static PlanInput sample() {
    PlanInput in;
    in.ifIndex = 34;
    in.hosts = {QStringLiteral("live.wardogs.bulkhead.pragmaengine.com"), QStringLiteral("firstlook.gg"),
                QStringLiteral("api.epicgames.dev")};
    in.ips = {QStringLiteral("77.88.55.88/32")};
    in.excludeIps = {QStringLiteral("203.0.113.10/32")};
    in.strategy = *findStrategy(QStringLiteral("general"));
    in.binDir = QStringLiteral("C:/App/dpi");
    in.listDir = QStringLiteral("C:/Users/x/dpi");
    return in;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    // ── Каталог ──────────────────────────────────────────────────────────
    std::fputs("Каталог стратегий\n", stdout);
    is(QStringLiteral("стратегии есть"), !catalog().isEmpty());
    is(QStringLiteral("general находится по имени"), findStrategy(QStringLiteral("general")) != nullptr);
    is(QStringLiteral("несуществующая не находится"), findStrategy(QStringLiteral("нет-такой")) == nullptr);

    {
        // Метки времени TCP — системная настройка, которую Flowseal включает
        // молча. Без неё подделка с fooling=ts не остановится на фильтре, а
        // дойдёт до сервера: стратегия не бесполезна, а вредна. Пока модуль не
        // умеет спрашивать и откатывать, таких связок в каталоге нет.
        bool ts = false;
        for (const auto &s: catalog()) {
            for (const auto &a: s.tcpDesync + s.udpDesync) {
                if (a.contains(QStringLiteral("fooling=ts")) || a.contains(QStringLiteral(",ts"))) ts = true;
            }
        }
        is(QStringLiteral("ни одной стратегии с метками времени TCP"), !ts);
    }
    {
        // Каждая стратегия обязана называть свой источник: связка аргументов без
        // происхождения — это наша выдумка, выданная за проверенную сообществом.
        bool named = true;
        for (const auto &s: catalog()) {
            if (s.source.trimmed().isEmpty() || s.human.trimmed().isEmpty()) named = false;
        }
        is(QStringLiteral("у каждой стратегии есть источник и объяснение"), named);
    }
    {
        const auto exp = expandArgs({QStringLiteral("--x={BIN}f.bin")}, QStringLiteral("C:\\App\\dpi"));
        is(QStringLiteral("{BIN} раскрывается прямыми слэшами и со слэшем на конце"),
           exp.first() == QStringLiteral("--x=C:/App/dpi/f.bin"));
    }

    // ── Списки ───────────────────────────────────────────────────────────
    std::fputs("\nСписки\n", stdout);
    {
        const auto p = buildPlan(sample());
        is(QStringLiteral("список доменов не пуст"), !p.hostlistText.trimmed().isEmpty());
        is(QStringLiteral("список адресов не пуст"), !p.ipsetText.trimmed().isEmpty());
        is(QStringLiteral("список исключений не пуст"), !p.excludeText.trimmed().isEmpty());
        is(QStringLiteral("домен игры попал в список"),
           p.hostlistText.contains(QStringLiteral("live.wardogs.bulkhead.pragmaengine.com")));
        is(QStringLiteral("адрес сервера попал в исключения"),
           p.excludeText.contains(QStringLiteral("203.0.113.10/32")));
        is(QStringLiteral("каждый файл кончается переводом строки"),
           p.hostlistText.endsWith(QChar('\n')) && p.ipsetText.endsWith(QChar('\n'))
               && p.excludeText.endsWith(QChar('\n')));
    }
    {
        // Главная защита: даже когда защищать нечего, файл не должен быть пуст.
        PlanInput in = sample();
        in.hosts.clear();
        in.ips.clear();
        in.excludeIps.clear();
        const auto p = buildPlan(in);
        is(QStringLiteral("на пустом входе списки всё равно не пусты — иначе winws дурит всех"),
           !p.hostlistText.trimmed().isEmpty() && !p.ipsetText.trimmed().isEmpty()
               && !p.excludeText.trimmed().isEmpty());
        is(QStringLiteral("сторожевые строки — из зарезервированных диапазонов"),
           p.ipsetText.contains(QStringLiteral("203.0.113.")) && p.hostlistText.contains(QStringLiteral(".invalid")));
    }

    // ── Аргументы ────────────────────────────────────────────────────────
    std::fputs("\nАргументы\n", stdout);
    {
        const auto p = buildPlan(sample());
        const auto &a = p.args;

        is(QStringLiteral("адаптер ограничен через --wf-iface"), a.contains(QStringLiteral("--wf-iface=34.0")));
        {
            bool raw = false;
            for (const auto &x: a) {
                if (x.startsWith(QStringLiteral("--wf-raw"))) raw = true;
            }
            // См. ловушку 2 в шапке: это «или», а не «и».
            is(QStringLiteral("--wf-raw-part не используется НИКОГДА"), !raw);
        }
        is(QStringLiteral("--wf-iface стоит раньше всех фильтров"),
           at(a, QStringLiteral("--wf-iface=34.0")) == 0);

        is(QStringLiteral("захват TCP ограничен портами"), a.contains(QStringLiteral("--wf-tcp=80,443,8443")));
        is(QStringLiteral("захват UDP ограничен портом"), a.contains(QStringLiteral("--wf-udp=443")));
        {
            // Игровые 1024-65535 сюда не входят намеренно: подделка без TTL
            // внутри живой игровой сессии — это мусор, посланный игровому
            // серверу, а не обход фильтра.
            bool wide = false;
            for (const auto &x: a) {
                if (x.startsWith(QStringLiteral("--wf-tcp=")) && x.contains(QStringLiteral("65535"))) wide = true;
                if (x.startsWith(QStringLiteral("--wf-udp=")) && x.contains(QStringLiteral("65535"))) wide = true;
            }
            is(QStringLiteral("весь диапазон портов не захватывается"), !wide);
        }

        // Ловушка 3: домены и адреса — разными профилями.
        is(QStringLiteral("профили разделены через --new"), a.contains(QStringLiteral("--new")));
        {
            const int h = at(a, QStringLiteral("--hostlist=C:/Users/x/dpi/bypass.hosts"));
            const int ipset = at(a, QStringLiteral("--ipset=C:/Users/x/dpi/bypass.ips"));
            const int sep = at(a, QStringLiteral("--new"));
            is(QStringLiteral("hostlist и ipset лежат в РАЗНЫХ профилях"),
               h >= 0 && ipset >= 0 && sep > h && sep < ipset);
        }
        {
            // Исключение обязано быть в КАЖДОМ профиле: попав в один, оно
            // защитит наш туннель ровно наполовину.
            int profiles = 1;
            int excludes = 0;
            for (const auto &x: a) {
                if (x == QStringLiteral("--new")) profiles++;
                if (x.startsWith(QStringLiteral("--ipset-exclude="))) excludes++;
            }
            is(QStringLiteral("--ipset-exclude есть в каждом профиле"), excludes == profiles);
        }
        {
            bool tcp = false, udp = false;
            for (const auto &x: a) {
                if (x.startsWith(QStringLiteral("--filter-tcp="))) tcp = true;
                if (x.startsWith(QStringLiteral("--filter-udp="))) udp = true;
            }
            is(QStringLiteral("есть профиль TCP и профиль UDP"), tcp && udp);
        }
        {
            // Заготовка обязана лежать в каталоге бинарей, а не где придётся:
            // winws с ненайденным файлом падает на старте.
            bool ok = true;
            for (const auto &x: a) {
                if (!x.contains(QStringLiteral(".bin"))) continue;
                if (!x.contains(QStringLiteral("C:/App/dpi/"))) ok = false;
            }
            is(QStringLiteral("все заготовки берутся из каталога модуля"), ok);
        }
        is(QStringLiteral("путей с обратными слэшами в аргументах нет"),
           !a.join(QChar(' ')).contains(QChar('\\')));
    }
    {
        // Стратегия без UDP не должна порождать пустой профиль: winws на
        // профиле без --dpi-desync ничего не делает, но захват остаётся.
        PlanInput in = sample();
        in.strategy.udpDesync.clear();
        const auto p = buildPlan(in);
        bool udp = false;
        for (const auto &x: p.args) {
            if (x.startsWith(QStringLiteral("--wf-udp")) || x.startsWith(QStringLiteral("--filter-udp"))) udp = true;
        }
        is(QStringLiteral("без стратегии UDP профиль UDP не создаётся"), !udp);
    }
    {
        PlanInput in = sample();
        in.ifIndex = 0;
        const auto p = buildPlan(in);
        bool iface = false;
        for (const auto &x: p.args) {
            if (x.startsWith(QStringLiteral("--wf-iface"))) iface = true;
        }
        is(QStringLiteral("без индекса адаптера --wf-iface не подставляется пустым"), !iface);
    }

    // ── Отпечаток ────────────────────────────────────────────────────────
    std::fputs("\nОтпечаток плана\n", stdout);
    {
        const auto a = buildPlan(sample());
        const auto b = buildPlan(sample());
        is(QStringLiteral("тот же вход — тот же отпечаток"), a.hash == b.hash && !a.hash.isEmpty());

        PlanInput other = sample();
        other.hosts << QStringLiteral("example.org");
        is(QStringLiteral("другой список — другой отпечаток"), buildPlan(other).hash != a.hash);

        PlanInput iface = sample();
        iface.ifIndex = 35;
        is(QStringLiteral("другой адаптер — другой отпечаток"), buildPlan(iface).hash != a.hash);

        PlanInput strat = sample();
        strat.strategy = *findStrategy(QStringLiteral("alt11-badseq"));
        is(QStringLiteral("другая стратегия — другой отпечаток"), buildPlan(strat).hash != a.hash);

        // Отпечаток решает, перезапускать ли winws, а перезапуск — это выгрузка
        // и загрузка драйвера. Совпал бы он при разных исключениях — и смена
        // сервера оставила бы наш собственный туннель под порчей.
        PlanInput excl = sample();
        excl.excludeIps = {QStringLiteral("198.51.100.7/32")};
        is(QStringLiteral("другие исключения — другой отпечаток"), buildPlan(excl).hash != a.hash);
    }

    // ── Перевод списка «мимо туннеля» ────────────────────────────────────
    std::fputs("\nДомены из схемы маршрутов\n", stdout);
    {
        QStringList uncovered;
        const auto hosts = hostsFromDirectDomains(QStringLiteral("domain:epicgames.com\n"
                                                                 "full:api.epicgames.dev\n"
                                                                 "geosite:category-games\n"
                                                                 "keyword:steam\n"
                                                                 "regexp:.*wardogs.*\n"
                                                                 "# комментарий\n"
                                                                 "firstlook.gg\n"
                                                                 "domain:ru\n"
                                                                 "domain:xn--p1ai\n"),
                                                  &uncovered);
        is(QStringLiteral("«domain:» становится обычной строкой"), hosts.contains(QStringLiteral("epicgames.com")));
        is(QStringLiteral("«full:» становится строкой с «^»"),
           hosts.contains(QStringLiteral("^api.epicgames.dev")));
        is(QStringLiteral("голое имя проходит как есть"), hosts.contains(QStringLiteral("firstlook.gg")));
        is(QStringLiteral("комментарий отброшен"), !hosts.contains(QStringLiteral("# комментарий")));

        // Зона верхнего уровня в списке защиты значит «дурить весь рунет»: это
        // не защита игры, а нагрузка на процессор и лишний повод для фильтра.
        is(QStringLiteral("зоны верхнего уровня в список не попадают"),
           !hosts.contains(QStringLiteral("ru")) && !hosts.contains(QStringLiteral("xn--p1ai")));

        // Молчать о невыразимом нельзя: человек увидел бы «geosite:games» в
        // схеме и решил, что игры защищены.
        is(QStringLiteral("geosite назван невыразимым"),
           uncovered.contains(QStringLiteral("geosite:category-games")));
        is(QStringLiteral("keyword назван невыразимым"), uncovered.contains(QStringLiteral("keyword:steam")));
        is(QStringLiteral("regexp назван невыразимым"),
           uncovered.contains(QStringLiteral("regexp:.*wardogs.*")));
        is(QStringLiteral("отброшенные зоны тоже названы"),
           uncovered.contains(QStringLiteral("domain:ru")) && uncovered.contains(QStringLiteral("domain:xn--p1ai")));
    }
    {
        QStringList uncovered;
        const auto hosts = hostsFromDirectDomains(QString(), &uncovered);
        is(QStringLiteral("пустая схема даёт пустой список, а не мусор"), hosts.isEmpty());
    }

    // ── Правила модуля, которые живут в коде, а не в плане ───────────────
    //
    // Проверяются ПО ИСХОДНИКУ намеренно. Всё это — поведение при событиях,
    // которых на сборочной машине не бывает: запущенный античит, чужой
    // перехватчик, упавший winws. Стенда для них нет, а молча потерять любое
    // из этих правил при переделке легко — и заметит это первым тот, у кого
    // отберут игровой аккаунт.
    std::fputs("\nПравила модуля (по исходнику)\n", stdout);
    {
        const auto src = slurp(QStringLiteral("dpi/DpiModule.cpp"));
        is(QStringLiteral("исходник модуля найден"), !src.isEmpty());
        if (!src.isEmpty()) {
            // ПРАВИЛО 2: не стартуем при античите и гаснем при его появлении.
            is(QStringLiteral("античиты перечислены поимённо"),
               src.contains(QStringLiteral("EasyAntiCheat.exe")) && src.contains(QStringLiteral("BEService.exe"))
                   && src.contains(QStringLiteral("vgc.exe")));
            is(QStringLiteral("Elytra ловится по слову: имя её процесса заранее неизвестно"),
               src.contains(QStringLiteral("elytra")));
            is(QStringLiteral("при античите модуль гасится, а не предупреждает"),
               src.contains(QStringLiteral("State::Blocked")));
            is(QStringLiteral("сторож проверяет античиты на каждом тике"),
               src.contains(QStringLiteral("antiCheatsRunning(runningPrograms())")));

            // ПРАВИЛО 3: рядом с чужим перехватчиком не запускаемся и ничего
            // чужого не трогаем. Для уборки есть «Починить сеть», и она теперь
            // отличает наш каталог от чужого.
            is(QStringLiteral("чужие перехватчики перечислены"),
               src.contains(QStringLiteral("goodbyedpi.exe")) && src.contains(QStringLiteral("ciadpi.exe")));
            is(QStringLiteral("чужой перехватчик — отказ, а не убийство чужого процесса"),
               src.contains(QStringLiteral("State::Foreign")) && !src.contains(QStringLiteral("taskkill")));

            // ПРАВИЛО 1: управляем драйвером, а не процессом. Античиты смотрят
            // на резидентный WinDivert, а не на winws.
            is(QStringLiteral("драйвер снимается, а не только останавливается"),
               src.contains(QStringLiteral("\"delete\"")) && src.contains(QStringLiteral("WinDivert")));
            {
                const int at = src.indexOf(QStringLiteral("void DpiModule::stop("));
                is(QStringLiteral("остановка выгружает драйвер"),
                   at >= 0 && src.mid(at, 800).contains(QStringLiteral("unloadDriver()")));
            }
            {
                const int at = src.indexOf(QStringLiteral("void DpiModule::shutdown()"));
                is(QStringLiteral("выход из клиента выгружает драйвер"),
                   at >= 0 && src.mid(at, 500).contains(QStringLiteral("unloadDriver()")));
            }

            // ПРАВИЛО 6: winws не переживает клиента.
            is(QStringLiteral("winws живёт в Job с KILL_ON_JOB_CLOSE"),
               src.contains(QStringLiteral("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE")));

            // Обхода античита у нас нет и не будет: переименование драйвера —
            // это ровно то, за что банят, и делать этого мы не станем.
            is(QStringLiteral("драйвер не переименовывается и не прячется"),
               !src.contains(QStringLiteral("windivert-hide")) && !src.contains(QStringLiteral("WinDivertHide")));

            // ПРАВИЛО 4: ярус ядра выключается на время работы модуля.
            is(QStringLiteral("модуль объявляет себя активным для ConfigBuilder"),
               src.contains(QStringLiteral("dpi_module_active = true")));
            is(QStringLiteral("и снимает признак при остановке"),
               src.contains(QStringLiteral("dpi_module_active = false")));
        }
    }
    {
        const auto src = slurp(QStringLiteral("db/ConfigBuilder.cpp"));
        is(QStringLiteral("ядро не дробит приветствие, пока работает модуль"),
           src.contains(QStringLiteral("dpi_fragment && !dataStore->dpi_module_active")));
    }

    std::fputs((QStringLiteral("\nПроверок: %1, провалов: %2\n").arg(checks).arg(fails)).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
