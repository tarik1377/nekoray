/**
 * ОДНА ПАЛИТРА НА ВЕСЬ КЛИЕНТ.
 *
 * ЖИВОЙ СЛУЧАЙ. 25.09.2026 владелец принял «лесной» вид из PR #38 и попросил
 * сделать так же везде. Цвета жили копиями, и после смены палитры прежний
 * зелёный акцент остался в журнале, трее, пилюле состояния, окне первого запуска
 * и панели «Зелёный Ритм» — двенадцать мест, которые видно только на снимке.
 * Теперь код берёт цвета из ui/Palette.hpp, а вторая и последняя копия — таблица
 * стилей modern.css.
 *
 * Проверяется:
 *  — основные цвета Palette.hpp те же, что в modern.css;
 *  — ни в коде окон, ни в таблице стилей, ни в знаке панели нет цветов прежней
 *    палитры — ни строкой, ни тройкой чисел;
 *  — каждая картинка, на которую ссылается modern.css, есть файлом и вписана в
 *    qss.qrc: без записи в ресурсах переключатель молча рисовался бы пустотой;
 *  — лесная тема у каждого: до 16.07.2026 по умолчанию стояла системная, и
 *    JsonStore сохранил её в настройки всех, кто ставил программу тогда. У них
 *    окно выходило наполовину — колонка лесная, диалоги, списки и меню
 *    системные. Теперь разовый перевод на лесную и никакого выбора темы: в
 *    старом диалоге лесную было не выбрать вовсе, а системную — одним щелчком.
 *
 * Только чтение файлов: ни окон, ни сети.
 *
 * Запуск: ninja palette_test && ./palette_test
 */

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
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

static QString rootDir() {
    for (const QString &prefix: {QStringLiteral(""), QStringLiteral("../"), QStringLiteral("../../")}) {
        if (QFile::exists(prefix + QStringLiteral("ui/Palette.hpp"))) return prefix;
    }
    return {};
}

static QString slurp(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString r = rootDir();
    is(QStringLiteral("корень дерева найден"), !r.isEmpty() || QFile::exists(QStringLiteral("ui/Palette.hpp")));

    const QString palette = slurp(r + QStringLiteral("ui/Palette.hpp"));
    const QString css = slurp(r + QStringLiteral("res/theme/feiyangqingyun/qss/modern.css"));
    const QString qrc = slurp(r + QStringLiteral("res/theme/feiyangqingyun/qss.qrc"));
    is(QStringLiteral("Palette.hpp, modern.css и qss.qrc прочитаны"),
       !palette.isEmpty() && !css.isEmpty() && !qrc.isEmpty());

    // ---- 1. ДВЕ КОПИИ СОВПАДАЮТ ----
    QHash<QString, QString> tokens;
    const QRegularExpression token(QStringLiteral(R"re(\b(k\w+)\s*=\s*"(#[0-9a-fA-F]{6})")re"));
    for (auto it = token.globalMatch(palette); it.hasNext();) {
        const auto m = it.next();
        tokens.insert(m.captured(1), m.captured(2).toLower());
    }
    is(QStringLiteral("в Palette.hpp есть токены"), tokens.size() >= 10);
    for (const char *name: {"kAccent", "kSidebar", "kSurface", "kSurfaceUp", "kLine", "kDim", "kText", "kMuted"}) {
        const QString key = QString::fromLatin1(name);
        is(QStringLiteral("%1 %2 — есть и в modern.css").arg(key, tokens.value(key)),
           tokens.contains(key) && css.contains(tokens.value(key), Qt::CaseInsensitive));
    }

    // ---- 2. ПРЕЖНЯЯ ПАЛИТРА НЕ ВЕРНУЛАСЬ ----
    // Строкой и тройкой чисел: в коде цвет пишут обоими способами.
    const QList<QRegularExpression> old{
        QRegularExpression(QStringLiteral("#?\\b(3fb950|2ea043|4ac95c|2c974b|9aa0a8|e4e6eb|0d1117|454c55|c3c8d0|"
                                          "181b20|1e2126|262a30|22262b|282d33|2f343b|5a5f66|1a1d22|6a7078|8b949e)\\b"),
                           QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("0x3f,\\s*0xb9,\\s*0x50"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("0x9a,\\s*0xa0,\\s*0xa8"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("0x5a,\\s*0x5f,\\s*0x66"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("0x1a,\\s*0x1d,\\s*0x22"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("0x6a,\\s*0x70,\\s*0x78"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("0x8b,\\s*0x94,\\s*0x9e"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("\\b63,\\s*185,\\s*80\\b")),
    };
    QStringList files{r + QStringLiteral("res/theme/feiyangqingyun/qss/modern.css"),
                      r + QStringLiteral("res/icon/gr-panel.svg")};
    for (const QString &dir: {QStringLiteral("ui"), QStringLiteral("db")}) {
        QDirIterator it(r + dir, {QStringLiteral("*.cpp"), QStringLiteral("*.hpp"), QStringLiteral("*.h"),
                                  QStringLiteral("*.ui")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files << it.next();
    }
    int scanned = 0;
    QStringList offenders;
    for (const QString &file: files) {
        const QString text = slurp(file);
        if (text.isEmpty()) continue;
        scanned++;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); i++) {
            for (const auto &re: old) {
                if (re.match(lines[i]).hasMatch()) {
                    offenders << QStringLiteral("%1:%2").arg(QString(file).remove(0, r.size())).arg(i + 1);
                    break;
                }
            }
        }
    }
    is(QStringLiteral("просмотрено файлов окна: %1").arg(scanned), scanned > 40);
    is(QStringLiteral("цветов прежней палитры нет%1")
           .arg(offenders.isEmpty() ? QString() : QStringLiteral(": ") + offenders.mid(0, 8).join(QStringLiteral(", "))),
       offenders.isEmpty());

    // ---- 3. КАРТИНКИ ТАБЛИЦЫ СТИЛЕЙ НА МЕСТЕ ----
    const QString prefix = QStringLiteral(":/themes/feiyangqingyun/");
    int urls = 0;
    for (auto it = QRegularExpression(QStringLiteral(R"re(url\((:/themes/feiyangqingyun/[^)]+)\))re")).globalMatch(css);
         it.hasNext();) {
        const QString rel = it.next().captured(1).mid(prefix.size());
        urls++;
        is(QStringLiteral("%1 — файлом").arg(rel), QFile::exists(r + QStringLiteral("res/theme/feiyangqingyun/") + rel));
        is(QStringLiteral("%1 — в qss.qrc").arg(rel), qrc.contains(QStringLiteral("<file>%1</file>").arg(rel)));
    }
    is(QStringLiteral("переключатели флажков подключены (картинок: %1)").arg(urls), urls >= 7);

    // ---- 4. ЛЕСНАЯ ТЕМА — У КАЖДОГО ----
    const auto text = [&](const QString &rel) {
        return slurp(r + rel).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    };
    const QString store = text(QStringLiteral("main/NekoGui_DataStore.hpp"));
    const QString config = text(QStringLiteral("main/NekoGui.cpp"));
    const QString window = text(QStringLiteral("ui/mainwindow.cpp"));
    const QString themes = text(QStringLiteral("ui/ThemeManager.cpp"));
    const QString form = text(QStringLiteral("ui/dialog_basic_settings.ui"));
    const QString dialog = text(QStringLiteral("ui/dialog_basic_settings.cpp"));
    const int forest = themes.indexOf(QStringLiteral("case 4:"));
    is(QStringLiteral("тема 4 — modern.css, лесная"),
       forest > 0 && themes.mid(forest, themes.indexOf(QStringLiteral("break;"), forest) - forest)
                         .contains(QStringLiteral("qss/modern.css")));
    is(QStringLiteral("новым установкам — лесная по умолчанию"), store.contains(QStringLiteral("QString theme = \"4\";")));
    is(QStringLiteral("флаг разового перевода хранится в настройках"),
       store.contains(QStringLiteral("bool theme_forest_migrated = false;")) &&
           config.contains(QStringLiteral("configItem(\"theme_forest_migrated\", &theme_forest_migrated")));
    const int migrate = window.indexOf(QStringLiteral("if (!NekoGui::dataStore->theme_forest_migrated) {"));
    const int apply = window.indexOf(QStringLiteral("themeManager->ApplyTheme(NekoGui::dataStore->theme);"));
    is(QStringLiteral("давних переводят на лесную раньше, чем тема применится"),
       migrate > 0 && apply > migrate &&
           window.mid(migrate, apply - migrate).contains(QStringLiteral("NekoGui::dataStore->theme = QStringLiteral(\"4\");")));
    is(QStringLiteral("в «Дополнительных настройках» нет выбора темы"),
       !form.isEmpty() && !form.contains(QStringLiteral("name=\"theme\"")) &&
           !dialog.contains(QStringLiteral("ui->theme")) && !dialog.contains(QStringLiteral("ApplyTheme")));
    is(QStringLiteral("«Задать иконку» осталась"), form.contains(QStringLiteral("name=\"set_custom_icon\"")));

    std::fputs(QStringLiteral("\nпроверок %1, провалов %2\n").arg(checks).arg(fails).toUtf8().constData(), stdout);
    return fails == 0 ? 0 : 1;
}
