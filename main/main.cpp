#include <csignal>

#include <QApplication>
#include <QDir>
#include <QTranslator>
#include <QMessageBox>
#include <QStandardPaths>
#include <QLocalSocket>
#include <QLocalServer>
#include <QThread>
#include <QTimer>

#include "3rdparty/RunGuard.hpp"
#include "main/NekoGui.hpp"

#include "ui/mainwindow_interface.h"

#ifdef Q_OS_WIN
#include "sys/windows/MiniDump.h"
#endif

void signal_handler(int signum) {
    if (qApp) {
        GetMainWindow()->on_commitDataRequest();
        qApp->exit();
    }
}

QTranslator* trans = nullptr;
QTranslator* trans_qt = nullptr;

void loadTranslate(const QString& locale) {
    if (trans != nullptr) {
        trans->deleteLater();
    }
    if (trans_qt != nullptr) {
        trans_qt->deleteLater();
    }
    //
    trans = new QTranslator;
    trans_qt = new QTranslator;
    QLocale::setDefault(QLocale(locale));
    //
    if (trans->load(":/translations/" + locale + ".qm")) {
        QCoreApplication::installTranslator(trans);
    }
    if (trans_qt->load(QApplication::applicationDirPath() + "/qtbase_" + locale + ".qm")) {
        QCoreApplication::installTranslator(trans_qt);
    }
}

// Имя местного канала, по которому второй запуск отдаёт ссылку первому.
//
// Менять его безопасно: канал живёт ровно столько, сколько запущена программа,
// и на диске ничего не оставляет. Единственная цена — во время обновления
// старый и новый экземпляр не увидят друг друга, и второй решит, что он первый.
// Это одна минута на переустановку против имени чужого проекта навсегда.
#define LOCAL_SERVER_PREFIX "greenrhythmlocalserver-"

int main(int argc, char* argv[]) {
    // Core dump
#ifdef Q_OS_WIN
    Windows_SetCrashHandler();
#endif

    // pre-init QApplication
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0) && QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 7, 0)
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
#endif
    QApplication::setQuitOnLastWindowClosed(false);
    auto preQApp = new QApplication(argc, argv);

    // Clean
    QDir::setCurrent(QApplication::applicationDirPath());
    if (QFile::exists("updater.old")) {
        QFile::remove("updater.old");
    }
#ifndef Q_OS_WIN
    if (!QFile::exists("updater")) {
        QFile::link("launcher", "updater");
    }
#endif

    // Flags
    NekoGui::dataStore->argv = QApplication::arguments();
    if (NekoGui::dataStore->argv.contains("-many")) NekoGui::dataStore->flag_many = true;
    if (NekoGui::dataStore->argv.contains("-appdata")) {
        NekoGui::dataStore->flag_use_appdata = true;
        int appdataIndex = NekoGui::dataStore->argv.indexOf("-appdata");
        if (NekoGui::dataStore->argv.size() > appdataIndex + 1 && !NekoGui::dataStore->argv.at(appdataIndex + 1).startsWith("-")) {
            NekoGui::dataStore->appdataDir = NekoGui::dataStore->argv.at(appdataIndex + 1);
        }
    }
    if (NekoGui::dataStore->argv.contains("-tray")) NekoGui::dataStore->flag_tray = true;
    if (NekoGui::dataStore->argv.contains("-debug")) NekoGui::dataStore->flag_debug = true;
    if (NekoGui::dataStore->argv.contains("-flag_restart_tun_on")) NekoGui::dataStore->flag_restart_tun_on = true;
    if (NekoGui::dataStore->argv.contains("-flag_reorder")) NekoGui::dataStore->flag_reorder = true;

    // greenrhythm:// deep link from the OS scheme handler. Raw and UNTRUSTED here —
    // only a length cap; full validation happens in MainWindow::import_scheme_url.
    QString scheme_arg;
    for (const auto &arg: NekoGui::dataStore->argv) {
        if (arg.startsWith("greenrhythm://", Qt::CaseInsensitive)) {
            if (arg.size() <= 16 * 1024) scheme_arg = arg;
            break;
        }
    }
#ifdef NKR_CPP_USE_APPDATA
    NekoGui::dataStore->flag_use_appdata = true; // Example: Package & MacOS
#endif
#ifdef NKR_CPP_DEBUG
    NekoGui::dataStore->flag_debug = true;
#endif

    // dirs & clean
    auto wd = QDir(QApplication::applicationDirPath());
    if (NekoGui::dataStore->flag_use_appdata) {
        QApplication::setApplicationName("greenrhythm");
        if (!NekoGui::dataStore->appdataDir.isEmpty()) {
            wd.setPath(NekoGui::dataStore->appdataDir);
        } else {
            wd.setPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
        }
    }
    if (!wd.exists()) wd.mkpath(wd.absolutePath());
    if (!wd.exists("config")) wd.mkdir("config");
    QDir::setCurrent(wd.absoluteFilePath("config"));
    QDir("temp").removeRecursively();

    // init QApplication
    delete preQApp;
    QApplication a(argc, argv);

    // dispatchers
    DS_cores = new QThread;
    DS_cores->start();

    // RunGuard
    // Тот же довод, что и у местного канала: страж одиночного запуска ничего не
    // хранит между запусками.
    RunGuard guard("greenrhythm" + wd.absolutePath());
    quint64 guard_data_in = GetRandomUint64();
    quint64 guard_data_out = 0;
    if (!NekoGui::dataStore->flag_many && !guard.tryToRun(&guard_data_in)) {
        // Some Good System
        if (guard.isAnotherRunning(&guard_data_out)) {
            // Wake up a running instance
            QLocalSocket socket;
            socket.connectToServer(LOCAL_SERVER_PREFIX + Int2String(guard_data_out));
            qDebug() << socket.fullServerName();
            if (!socket.waitForConnected(500)) {
                qDebug() << "Failed to wake a running instance.";
                return 0;
            }
            qDebug() << "connected to local server, try to raise another program";
            // Hand the deep link to the running instance (it validates and imports).
            if (!scheme_arg.isEmpty()) {
                socket.write(scheme_arg.toUtf8());
                socket.flush();
                socket.waitForBytesWritten(1000);
            }
            socket.disconnectFromServer();
            return 0;
        }
        // Some Bad System
        QMessageBox::warning(nullptr, "NekoGui", "RunGuard disallow to run, use -many to force start.");
        return 0;
    }
    MF_release_runguard = [&] { guard.release(); };

#ifdef Q_OS_MACOS
    /*
     * ЗАПУЩЕНЫ ЛИ МЫ ИЗ КАРАНТИННОЙ КОПИИ.
     *
     * Приложение не подписано, и система это знает. Скачанное из интернета и
     * запущенное прямо из «Загрузок», оно исполняется не оттуда, где лежит, а
     * из ТОЛЬКО ДЛЯ ЧТЕНИЯ копии в /private/var/folders/.../AppTranslocation/.
     * Настройки писать некуда, и человек получает клиент, который не запоминает
     * ни серверов, ни активации — и не говорит, почему.
     *
     * Лечится это одним действием, и сказать о нём надо как об ОЖИДАЕМОМ ШАГЕ,
     * а не как об ошибке: половина людей, увидев слово «ошибка» у неподписанной
     * программы, решит, что скачала вирус, — и будет права осторожничать.
     */
    if (QApplication::applicationDirPath().contains("/AppTranslocation/")) {
        QMessageBox box(QMessageBox::Information, "GreenRhythm",
                        QObject::tr("Перетащите GreenRhythm в папку «Программы»"),
                        QMessageBox::Ok, nullptr);
        box.setInformativeText(
            QObject::tr("Пока приложение запускается из «Загрузок», macOS открывает его копию "
                        "только для чтения — и оно не сможет запомнить ни серверы, ни вход.\n\n"
                        "Это обычное поведение системы для программ, скачанных из интернета, "
                        "а не ошибка.\n\n"
                        "Перенесите GreenRhythm в «Программы» и запустите оттуда."));
        box.exec();
        return 0;
    }
#endif

// icons
#if (QT_VERSION >= QT_VERSION_CHECK(5, 11, 0))
    QIcon::setFallbackSearchPaths(QStringList{
        ":/neko",
        ":/icon",
    });
#endif

    // icon for no theme
    if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName("breeze");
    }

    // Dir
    QDir dir;
    bool dir_success = true;
    if (!dir.exists("profiles")) {
        dir_success &= dir.mkdir("profiles");
    }
    if (!dir.exists("groups")) {
        dir_success &= dir.mkdir("groups");
    }
    if (!dir.exists(ROUTES_PREFIX_NAME)) {
        dir_success &= dir.mkdir(ROUTES_PREFIX_NAME);
    }
    if (!dir_success) {
        QMessageBox::warning(nullptr, "Error", "No permission to write " + dir.absolutePath());
        return 1;
    }

    /*
     * НАБОРЫ ПРАВИЛ ПЕРЕНОСЯТСЯ ТУДА, ГДЕ ИХ ИЩЕТ ЯДРО.
     *
     * Ядро наследует рабочий каталог приложения, а это каталог НАСТРОЕК. На
     * Windows он совпадает с местом установки, и файлы, положенные туда
     * выкладкой, находятся сами собой. Там, где настройки живут отдельно от
     * программы (macOS с NKR_CPP_USE_APPDATA, пакетные сборки), совпадения нет:
     * выкладка кладёт .srs внутрь бандла, а ядро смотрит в ~/Library/…, ничего
     * не находит и подставляет удалённый источник.
     *
     * Чем это оборачивается у человека: правила «российское — напрямую» и
     * блокировка рекламы не действуют до первой закачки, а закачка идёт ЧЕРЕЗ
     * САМ ТУННЕЛЬ. Он видит, что российские сайты какое-то время идут через
     * прокси — медленно, а иногда и с проверкой «вы не из России».
     *
     * Копируется один раз и только недостающее: свои правила человека не
     * трогаем, а перезапись при каждом запуске стирала бы его настройку.
     */
    {
        const QDir shipped(QApplication::applicationDirPath() + "/config");
        if (shipped.exists() && shipped.absolutePath() != QDir::currentPath()) {
            for (const auto &name: shipped.entryList({"*.srs"}, QDir::Files)) {
                if (QFile::exists(name)) continue;
                QFile::copy(shipped.absoluteFilePath(name), name);
            }

            /*
             * ПОСТАВЛЯЕМЫЕ УМОЛЧАНИЯ — ТОЖЕ, и раньше их здесь не было.
             *
             * Копировались только правила (.srs), а groups/nekobox.json нет. На
             * Windows это не замечалось: каталог настроек совпадает с местом
             * установки, и файл лежал там сам собой. На macOS настройки живут в
             * ~/Library/…, шаблон остаётся внутри пакета, и до приложения не
             * доходит НИКОГДА.
             *
             * Чем оборачивалось. В шаблоне ровно один ключ, и он не декоративный:
             * vpn_impl = 2, то есть смешанный сетевой стек туннеля. Умолчание в
             * коде — 0, gvisor, и на нём туннель отчитывается исправным, счётчики
             * идут, имена разрешаются, а страницы не открываются. Ровно этот отказ
             * описан в коммите 73c6818 по жалобе живого клиента. То есть Windows
             * получал рабочую настройку, а macOS — ту, на которой не работает.
             *
             * Копируется только НЕДОСТАЮЩЕЕ, как и правила выше: перезапись при
             * каждом запуске стирала бы всё, что человек настроил.
             */
            const QDir shippedGroups(shipped.absoluteFilePath("groups"));
            if (shippedGroups.exists()) {
                QDir().mkpath("groups");
                for (const auto &name: shippedGroups.entryList({"*.json"}, QDir::Files)) {
                    const QString target = "groups/" + name;
                    if (QFile::exists(target)) continue;
                    QFile::copy(shippedGroups.absoluteFilePath(name), target);
                }
            }
        }
    }

    // ИМЯ ФАЙЛА НАСТРОЕК — НАШЕ, НО СТАРОЕ ПОДБИРАЕТСЯ.
    //
    // Файл назывался groups/nekobox.json — по имени проекта, из которого клиент
    // вырос. Просто переименовать нельзя: у всех, кто уже поставил программу,
    // настройки лежат под старым именем, и новая сборка нашла бы пустоту —
    // пропали бы серверы, подписка, маршруты и всё остальное.
    //
    // Поэтому переход тихий и одноразовый: если нового файла ещё нет, а старый
    // есть — копируем. Именно КОПИРУЕМ, а не переименовываем: откат на прежнюю
    // сборку тогда останется возможным, и человек, которому новая не подошла,
    // не окажется без настроек вовсе.
    {
        const QString modern = QStringLiteral("groups/greenrhythm.json");
        const QString legacy = QStringLiteral("groups/nekobox.json");
        if (!QFile::exists(modern) && QFile::exists(legacy)) {
            if (QFile::copy(legacy, modern)) {
                qInfo() << "settings migrated from" << legacy << "to" << modern;
            } else {
                // Не вышло — остаёмся на старом имени. Молча начать с пустых
                // настроек хуже, чем сохранить прежнее имя файла.
                qWarning() << "settings migration failed, keeping" << legacy;
            }
        }
    }

    // Load dataStore
    switch (NekoGui::coreType) {
        case NekoGui::CoreType::SING_BOX:
            NekoGui::dataStore->fn = QFile::exists(QStringLiteral("groups/greenrhythm.json"))
                                         ? "groups/greenrhythm.json"
                                         : "groups/nekobox.json";
            break;
        default:
            MessageBoxWarning("Error", "Unknown coreType.");
            return 0;
    }
    auto isLoaded = NekoGui::dataStore->Load();
    if (!isLoaded) {
        NekoGui::dataStore->Save();
    }

    // Datastore & Flags
    if (NekoGui::dataStore->start_minimal) NekoGui::dataStore->flag_tray = true;

    // load routing
    // Preset 1 is the RU preset — QUIC blocked with the public resolvers exempted first,
    // domestic domains and game/anti-cheat traffic direct, local DNS for direct traffic.
    // This used to build preset 0 (an empty rule set), so none of that reached a fresh
    // install; the preset was only ever applied by hand from the routing dialog.
    NekoGui::dataStore->routing = std::make_unique<NekoGui::Routing>(1);
    NekoGui::dataStore->routing->fn = ROUTES_PREFIX + NekoGui::dataStore->active_routing;
    isLoaded = NekoGui::dataStore->routing->Load();
    if (!isLoaded) {
        NekoGui::dataStore->routing->Save();
    }

    // Translate
    QString locale;
    switch (NekoGui::dataStore->language) {
        case 1: // English
            break;
        case 2:
            locale = "zh_CN";
            break;
        case 3:
            locale = "fa_IR"; // farsi(iran)
            break;
        case 4:
            locale = "ru_RU"; // Russian
            break;
        default:
            locale = QLocale().name();
    }
    QGuiApplication::tr("QT_LAYOUT_DIRECTION");
    loadTranslate(locale);

    // Signals
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    // QLocalServer
    QLocalServer server;
    auto server_name = LOCAL_SERVER_PREFIX + Int2String(guard_data_in);
    QLocalServer::removeServer(server_name);
    server.listen(server_name);
    QObject::connect(&server, &QLocalServer::newConnection, &a, [&] {
        auto socket = server.nextPendingConnection();
        qDebug() << "nextPendingConnection:" << server_name << socket;
        // A second instance may push a greenrhythm:// deep link before it exits.
        // Accumulate — one write() is not guaranteed to arrive as one readyRead —
        // and only parse the whole message once the sender disconnects.
        auto buf = std::make_shared<QByteArray>();
        QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, buf] {
            if (buf->size() < 16 * 1024) buf->append(socket->readAll());
        });
        QObject::connect(socket, &QLocalSocket::disconnected, socket, [socket, buf] {
            auto data = QString::fromUtf8(buf->left(16 * 1024)).trimmed();
            if (data.startsWith("greenrhythm://", Qt::CaseInsensitive)) MW_dialog_message("", "SchemeImport#" + data);
            socket->deleteLater();
        });
        QTimer::singleShot(3000, socket, &QLocalSocket::deleteLater); // idle-connection safety net
        // raise main window
        MW_dialog_message("", "Raise");
    });

    UI_InitMainWindow();
    if (!scheme_arg.isEmpty()) {
        // Launched by a deep link with no instance running: import once the UI is up.
        QTimer::singleShot(500, [scheme_arg] { MW_dialog_message("", "SchemeImport#" + scheme_arg); });
    }
    return QApplication::exec();
}
