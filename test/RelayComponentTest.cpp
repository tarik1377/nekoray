/**
 * Проверка разбора ответов о компоненте резерва. Без сети и без диска.
 *
 * Набор существует ради одной ветки в первую очередь: 402 и 403 — разные вещи,
 * и перепутать их значит дать ВРЕДНЫЙ СОВЕТ, а не показать отказ. 402 это
 * «подписка кончилась», лечится продлением. 403 это «канал закрыт», подписка
 * при этом действующая, и предложение оплатить приведёт к тому, что человек
 * оплатит второй раз и придёт в поддержку выяснять, почему не помогло.
 *
 * Второе, ради чего он есть, — проверка суммы ДО скачивания. Без суммы
 * скачанное проверить нечем, а скачивается исполняемый файл.
 *
 * Запуск: ninja relay_component_test && ./relay_component_test
 */

#include "main/RelayComponent.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static int checks = 0;
static int fails = 0;

/** printf, а не qInfo: qInfo в сборке Release заглушён правилами логирования. */
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

/** Полное описание, какое отдаёт сайт. */
static QByteArray goodBody(const QString &sha = QString(64, QChar('a')), qint64 size = 8749568) {
    QJsonObject o;
    o["version"] = "1.0.0";
    o["sha256"] = sha;
    o["sizeBytes"] = static_cast<double>(size);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif
    say("");
    say("RelayComponent: разбор ответов");

    // ---- главное: 402 и 403 не путаются ----

    {
        const auto lapsed = RelayComponent::InterpretMeta(402, "{}", {});
        is("402 — отказ", !lapsed.res.ok);
        is("402 ведёт к продлению", lapsed.res.needsSubscription);

        const auto closed = RelayComponent::InterpretMeta(403, "{}", {});
        is("403 — отказ", !closed.res.ok);
        // Ровно то, ради чего набор заведён.
        is("403 НЕ ведёт к продлению", !closed.res.needsSubscription);
        is("403 не поминает подписку",
           !closed.res.detail.contains(QStringLiteral("подписк"), Qt::CaseInsensitive));
    }

    // ---- сеть отличается от осмысленного отказа ----

    {
        // При обрыве status равен нулю. Принять это за ответ значит показать
        // человеку выдуманную причину.
        const auto dead = RelayComponent::InterpretMeta(0, {}, QStringLiteral("Host not found"));
        is("обрыв — отказ", !dead.res.ok);
        is("обрыв не зовёт продлевать", !dead.res.needsSubscription);
    }

    // ---- «ещё не выложили» — не отказ человеку ----

    {
        const auto none = RelayComponent::InterpretMeta(204, {}, {});
        is("204 — не готово", !none.res.ok);
        is("204 не зовёт продлевать", !none.res.needsSubscription);
        is("204 говорит про подготовку",
           none.res.detail.contains(QStringLiteral("готовится")));
    }

    // ---- сумма проверяется ДО скачивания ----

    {
        const auto ok = RelayComponent::InterpretMeta(200, goodBody(), {});
        is("полное описание принято", ok.res.ok);
        is("сумма прочитана", ok.sha256.size() == 64);
        is("размер прочитан", ok.sizeBytes == 8749568);
        is("версия прочитана", ok.version == QStringLiteral("1.0.0"));

        // Без суммы проверять скачанное нечем.
        QJsonObject noSha;
        noSha["version"] = "1.0.0";
        noSha["sizeBytes"] = 100.0;
        is("без суммы — отказ",
           !RelayComponent::InterpretMeta(200, QJsonDocument(noSha).toJson(), {}).res.ok);

        is("сумма короче — отказ",
           !RelayComponent::InterpretMeta(200, goodBody(QString(63, QChar('a'))), {}).res.ok);
        is("сумма длиннее — отказ",
           !RelayComponent::InterpretMeta(200, goodBody(QString(65, QChar('a'))), {}).res.ok);

        // Строка нужной длины, но не сумма. Без этой проверки она доехала бы до
        // сравнения уже ПОСЛЕ того, как мегабайты скачаны.
        is("не шестнадцатеричная сумма — отказ",
           !RelayComponent::InterpretMeta(200, goodBody(QString(64, QChar('z'))), {}).res.ok);
        is("верхний регистр приводится",
           RelayComponent::InterpretMeta(200, goodBody(QString(64, QChar('A'))), {}).res.ok);

        is("нулевой размер — отказ", !RelayComponent::InterpretMeta(200, goodBody(QString(64, QChar('a')), 0), {}).res.ok);
        is("мусор вместо JSON — отказ",
           !RelayComponent::InterpretMeta(200, "не json вовсе", {}).res.ok);
    }

    // ---- платформа ----

    {
        // Пустая строка означала бы, что запрос уйдёт на адрес без платформы.
        is("платформа названа", !RelayComponent::PlatformId().isEmpty());
        // Совпадение с RELEASE_PLATFORMS на сайте — договор, а не совпадение.
        const QStringList known = {"windows-x64", "linux-x64", "macos-amd64", "macos-arm64"};
        is("платформа из списка сайта", known.contains(RelayComponent::PlatformId()));
    }

    say("");
    say(QString("проверок: %1, провалов: %2").arg(checks).arg(fails));
    std::fflush(stdout);
    return fails > 0 ? 1 : 0;
}
