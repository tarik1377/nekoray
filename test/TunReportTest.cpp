/**
 * Отчёт о туннеле не смеет врать про стек.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Выбранный человеком стек и стек, который реально
 * применит ядро, — разные вещи: normalizeTunStack в Go превращает «gvisor» и
 * пустое значение в «mixed», потому что чистый gvisor рвёт соединения на части
 * машин с Windows. «system» при этом уважается.
 *
 * Диагностика показывает оба значения, и второе она вычисляет СВОЕЙ копией того
 * же правила — на C++, в другом файле, на другом языке. Копия и оригинал могут
 * разойтись молча: правило поменяют в Go, отчёт продолжит показывать прежнее, и
 * поддержка будет искать беду по неверным данным. Причём отчёт — единственное,
 * чему в такой момент верят.
 *
 * Сторож читает ОБА файла и сверяет правило между ними, а не переписанные сюда
 * значения: переписанное — третья копия, расходится ровно так же, только теперь
 * с зелёными проверками.
 *
 * Запуск: ninja tun_report_test && ./tun_report_test
 */

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QString>

#include <cstdio>

static int checks = 0;
static int fails = 0;

static void is(const QString &what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::fputs((QString(ok ? QStringLiteral("  ok   ") : QStringLiteral("  ПРОВАЛ "))
                + what + QStringLiteral("\n"))
                   .toUtf8()
                   .constData(),
               stdout);
}

/**
 * Тело функции целиком: от заголовка до закрывающей скобки в нулевой колонке.
 *
 * Прежде здесь бралось фиксированное число символов — 320 для Go и 2600 для
 * C++, — подобранное под тогдашний вид файлов. Набор, который стережёт тихое
 * расхождение двух копий правила, умирал бы ровно тем же способом: молча и с
 * зелёными проверками, стоит функции подрасти или переехать.
 */
static QString functionBody(const QString &src, int at) {
    if (at < 0) return {};
    const int end = src.indexOf(QStringLiteral("\n}"), at);
    return end < 0 ? src.mid(at) : src.mid(at, end - at);
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

    const QString go = slurp(QStringLiteral("go/cmd/nekobox_core/neko_config_transform.go"));
    // Файл ищется по содержимому, а не по имени: mainwindow.cpp разрезан на
    // части, и диагностика уехала в свою. Набор, привязанный к имени файла,
    // сломался бы при следующем таком переезде — и это уже случилось.
    QString ui;
    for (const QString &candidate: {QStringLiteral("ui/Diagnostics.cpp"),
                                    QStringLiteral("ui/mainwindow.cpp")}) {
        const QString text = slurp(candidate);
        if (text.contains(QStringLiteral("MainWindow::tun_diagnostics_block"))) {
            ui = text;
            break;
        }
    }
    is(QStringLiteral("исходник ядра прочитан"), !go.isEmpty());
    is(QStringLiteral("исходник с блоком отчёта найден"), !ui.isEmpty());
    if (go.isEmpty() || ui.isEmpty()) {
        std::fputs("не нашёл исходники — запускать из дерева сборки\n", stdout);
        return 1;
    }

    // Что ядро подменяет: тело normalizeTunStack.
    const int at = go.indexOf(QStringLiteral("func normalizeTunStack"));
    is(QStringLiteral("normalizeTunStack найдена в ядре"), at >= 0);
    const QString fn = functionBody(go, at);

    const bool goMapsGvisor = fn.contains(QStringLiteral("\"gvisor\"")) && fn.contains(QStringLiteral("\"mixed\""));
    is(QStringLiteral("ядро подменяет gvisor на mixed"), goMapsGvisor);

    // Ядро НЕ трогает system: на этом держится ответ «system работать должен».
    is(QStringLiteral("ядро не подменяет system"), !fn.contains(QStringLiteral("\"system\"")));

    // Та же подмена обязана быть в отчёте.
    const int ub = ui.indexOf(QStringLiteral("MainWindow::tun_diagnostics_block"));
    is(QStringLiteral("блок отчёта найден в окне"), ub >= 0);
    const QString block = functionBody(ui, ub);

    is(QStringLiteral("отчёт знает про подмену gvisor на mixed"),
       block.contains(QStringLiteral("\"gvisor\"")) && block.contains(QStringLiteral("\"mixed\"")));

    // Оба значения, а не одно: ради этого набор и написан.
    is(QStringLiteral("отчёт печатает и выбранное, и применённое"),
       block.contains(QStringLiteral("chosen")) && block.contains(QStringLiteral("effective")));

    // Поля, без которых отчёт бесполезен для разбора TUN.
    for (const QString &field: {QStringLiteral("tun-requested"), QStringLiteral("tun-internal"),
                                QStringLiteral("platform-allows"), QStringLiteral("mtu"),
                                QStringLiteral("strict-route"), QStringLiteral("system=")}) {
        is(QStringLiteral("в отчёте есть поле ") + field, block.contains(field));
    }

    // На маке — признак перемещённой копии: самая частая причина «ничего не
    // работает», и снаружи она неотличима от поломки туннеля.
    is(QStringLiteral("отчёт ловит перемещённую копию на маке"),
       block.contains(QStringLiteral("AppTranslocation")));

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(), stdout);
    return fails == 0 ? 0 : 1;
}
