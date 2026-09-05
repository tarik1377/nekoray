/**
 * «Починить сеть» не должна сносить наш собственный обход и врать про чужие VPN.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Это самая разрушительная функция программы: она
 * останавливает службы, удаляет драйверы и требует прав администратора. Своего
 * набора у неё не было вовсе — ни одной проверки на 583 строки, — и обе беды
 * ниже прожили в ней незамеченными.
 *
 * ПЕРВАЯ. Перехватчики ищутся по маске winws|windivert|zapret|… в пути, имени и
 * описании службы. Маска правильная, но своего от чужого она не отличает:
 * драйвер WinDivert на машине один на всех, и служба у него одна. Появись у нас
 * собственный модуль обхода — эта же кнопка снесла бы его при первом нажатии.
 * Отличать можно только ПО ПУТИ: имя процесса и службы у нашего и чужого winws
 * одинаковы.
 *
 * ВТОРАЯ. Окно обещало «Другие VPN НЕ отключаются», а скрипт делал
 * Stop-Service -Force и Set-Service Disabled для warp, cloudflare, outline и
 * amnezia. Человек читал одно, получал другое — и в этой же функции рядом
 * записано, почему так нельзя: чужой туннель до дома или до работы выключать
 * втихую мы перестали ещё для адаптеров.
 *
 * Проверяется текст исходника, а не поведение: скрипт исполняется PowerShell'ом
 * под правами администратора, и запускать его из набора нельзя. Зато отношения
 * «исключение стоит во всех проходах» и «обещание совпадает со скриптом»
 * проверяются надёжно.
 *
 * Запуск: ninja network_repair_test && ./network_repair_test
 */

#include <QCoreApplication>
#include <QFile>
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

/** Строка админского скрипта, начинающаяся с заданного куска. */
static QString lineWith(const QString &src, const QString &needle) {
    const int at = src.indexOf(needle);
    if (at < 0) return {};
    const int from = src.lastIndexOf(QChar('\n'), at) + 1;
    int to = src.indexOf(QChar('\n'), at);
    if (to < 0) to = src.size();
    return src.mid(from, to - from);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    const QString src = slurp(QStringLiteral("ui/NetworkRepair.cpp"));
    is(QStringLiteral("исходник починки сети прочитан"), !src.isEmpty());
    if (src.isEmpty()) {
        std::fputs("не нашёл ui/NetworkRepair.cpp — запускать из дерева сборки\n", stdout);
        return 1;
    }

    // ---- СВОЁ ОТЛИЧАЕТСЯ ПО ПУТИ И ПОПАДАЕТ В СКРИПТ ----
    is(QStringLiteral("свой каталог вычисляется от места программы"),
       src.contains(QStringLiteral("applicationDirPath()")) && src.contains(QStringLiteral("\"/dpi\"")));
    is(QStringLiteral("свой каталог сравнивается в нижнем регистре"),
       src.contains(QStringLiteral(".toLower()")));
    is(QStringLiteral("одинарные кавычки в пути удваиваются"),
       src.contains(QStringLiteral("ownDir.replace(QStringLiteral(\"'\")")));
    is(QStringLiteral("свой каталог передаётся в скрипт как $Own"),
       src.contains(QStringLiteral("$Own='%2'")));

    // ---- ИСКЛЮЧЕНИЕ ВО ВСЕХ ЧЕТЫРЁХ ПРОХОДАХ ----
    //
    // Половина связки хуже, чем ничего: пропусти один проход — и модуль
    // выживет как служба, но погибнет как процесс, а человек увидит
    // «обход включён» без работающего обхода.
    struct Pass {
        const char *name;
        const char *needle;
    };
    const Pass passes[] = {
        {"службы перехватчиков", "Get-CimInstance Win32_Service | Where-Object {$_.PathName -match $t"},
        {"драйвер по имени WinDivert", "foreach($n in 'WinDivert','WinDivert1.4','WinDivert14')"},
        {"драйверы по пути", "Get-CimInstance Win32_SystemDriver | Where-Object {$_.PathName -match 'divert"},
        {"процессы перехватчиков", "Get-Process | Where-Object {$_.ProcessName -match 'winws"},
    };
    for (const auto &p: passes) {
        const QString line = lineWith(src, QString::fromUtf8(p.needle));
        is(QString::fromUtf8(p.name) + QStringLiteral(" — проход на месте"), !line.isEmpty());
        is(QString::fromUtf8(p.name) + QStringLiteral(" — своё исключается"),
           !line.isEmpty() && line.contains(QStringLiteral("$Own")));
        is(QString::fromUtf8(p.name) + QStringLiteral(" — своё попадает в отчёт"),
           !line.isEmpty() && line.contains(QStringLiteral("наш модуль обхода")));
    }

    // ---- ЧУЖИЕ VPN: ОБЕЩАНИЕ РАВНО СКРИПТУ ----
    const QString vpnLine =
        lineWith(src, QStringLiteral("$_.Name -match 'warp|cloudflare|outline|amnezia'"));
    is(QStringLiteral("проход по чужим VPN на месте"), !vpnLine.isEmpty());
    is(QStringLiteral("чужие VPN не останавливаются"),
       !vpnLine.isEmpty() && !vpnLine.contains(QStringLiteral("Stop-Service")));
    is(QStringLiteral("чужим VPN не меняют автозапуск"),
       !vpnLine.isEmpty() && !vpnLine.contains(QStringLiteral("Set-Service")));
    is(QStringLiteral("чужие VPN только называются в отчёте"),
       !vpnLine.isEmpty() && vpnLine.contains(QStringLiteral("НЕ тронут")));
    is(QStringLiteral("окно обещает, что чужие VPN не отключаются"),
       src.contains(QStringLiteral("Другие VPN НЕ отключаются")));

    // ---- ЧТО ОСТАЛОСЬ РАЗРУШИТЕЛЬНЫМ И ДОЛЖНО ТАКИМ ОСТАТЬСЯ ----
    //
    // Набор сторожит не «ничего не удалять», а «удалять только чужое». Пропади
    // удаление вовсе — кнопка перестанет делать то, ради чего заведена.
    is(QStringLiteral("чужие перехватчики по-прежнему удаляются"),
       src.contains(QStringLiteral("sc.exe delete")));
    is(QStringLiteral("файл hosts по-прежнему не трогается"),
       !src.contains(QStringLiteral("drivers\etc\hosts")));

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(),
               stdout);
    return fails == 0 ? 0 : 1;
}
