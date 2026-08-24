#include "RelayComponent.hpp"

#include "DeviceCredentials.hpp"
#include "HTTPRequestHelper.hpp"
#include "NekoGui.hpp"
#include "fmt/RelayBean.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>

namespace RelayComponent {

    namespace {
        const QString kSite = QStringLiteral("https://verdantvibe.ru");
        const QString kPath = QStringLiteral("/api/relay/component/");

        /** Потолок на скачивание. Больше настоящего с запасом, но конечный. */
        constexpr qint64 kMaxBytes = 64LL * 1024 * 1024;

        Result fail(const QString &detail, bool needsSub = false) {
            Result r;
            r.ok = false;
            r.detail = detail;
            r.needsSubscription = needsSub;
            return r;
        }

        /** Каталог для компонентов внутри рабочего. Создаётся при надобности. */
        QString componentsDir() {
            return QDir::current().absoluteFilePath(QStringLiteral("components"));
        }

        /** Сумма файла. Читается кусками: файл в памяти держать незачем. */
        QString sha256Of(const QString &path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (!h.addData(&f)) return {};
            return QString::fromLatin1(h.result().toHex());
        }

        /**
         * Сделать файл исполняемым там, где это требуется.
         *
         * Без этого запуск падает с отказом в доступе — сообщением, по которому
         * никто не догадается, что дело в правах только что скачанного файла.
         */
        bool makeRunnable(const QString &path) {
#ifdef Q_OS_WIN
            Q_UNUSED(path)
            return true;
#else
            QFile f(path);
            const auto want = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                              QFile::ReadUser | QFile::WriteUser | QFile::ExeUser;
            return f.setPermissions(want);
#endif
        }

        /** Запомнить путь, чтобы ExtraCore::Get его нашёл. */
        void registerPath(const QString &path) {
            auto obj = QString2QJsonObject(NekoGui::dataStore->extraCore->core_map);
            obj[NekoGui_fmt::kRelayCoreId] = path;
            NekoGui::dataStore->extraCore->core_map = QJsonObject2QString(obj, true);
            NekoGui::dataStore->extraCore->Save();
        }
    } // namespace

    QString InstalledPath() {
        auto name = QString::fromLatin1(NekoGui_fmt::kRelayCoreId);
#ifdef Q_OS_WIN
        name += QStringLiteral(".exe");
#endif
        return QDir(componentsDir()).absoluteFilePath(name);
    }

    bool IsInstalled() {
        return !NekoGui::dataStore->extraCore->Get(NekoGui_fmt::kRelayCoreId).isEmpty();
    }

    Result Install(const std::function<void(qint64, qint64)> &progress) {
        const auto platform = PlatformId();
        if (platform.isEmpty()) {
            return fail(QObject::tr("Резервное подключение для этой системы пока не собрано"));
        }

        const auto token = DeviceCredentials::Token();
        if (token.isEmpty()) {
            return fail(QObject::tr("Сначала введите код из личного кабинета"));
        }
        const QList<QPair<QByteArray, QByteArray>> auth = {
            {"Authorization", QByteArray("Bearer ") + token.toUtf8()}};

        // --- шаг 1: что выложено ---
        //
        // Тем же вызовом, что и сам файл: ему нужны и заголовок, и путь мимо
        // прокси, а описание — короткий ответ, для которого хватает потолка в
        // восемь килобайт. Заводить ради него третью разновидность запроса
        // значило бы держать две почти одинаковые ветки настройки прокси.
        QByteArray metaBytes;
        QBuffer metaSink(&metaBytes);
        metaSink.open(QIODevice::WriteOnly);
        const auto meta = NetworkRequestHelper::HttpDownload(
            QUrl(kSite + kPath + platform), &metaSink, 8 * 1024, auth);
        metaSink.close();

        // Разбор — в RelayComponentParse.cpp, там же и причины (402 против 403
        // это разные советы человеку, а не разные слова). Здесь только сеть.
        const auto parsed = InterpretMeta(meta.status, metaBytes, meta.error);
        if (!parsed.res.ok) return parsed.res;

        const auto version = parsed.version;
        const auto wantSha = parsed.sha256;
        const auto wantSize = parsed.sizeBytes;

        const auto target = InstalledPath();

        // Уже стоит нужное — не качать. Проверяется СУММА, а не номер версии:
        // номер лежал бы в настройках отдельно от файла и разошёлся бы с ним
        // при любой ручной правке каталога.
        if (QFile::exists(target) && sha256Of(target) == wantSha) {
            registerPath(target);
            Result r;
            r.ok = true;
            r.alreadyCurrent = true;
            r.detail = QObject::tr("Резервное подключение готово");
            return r;
        }

        if (!QDir().mkpath(componentsDir())) {
            return fail(QObject::tr("Не удалось создать папку для компонента"));
        }

        // --- шаг 2: сам файл ---
        //
        // Качается ПОД ВРЕМЕННЫМ ИМЕНЕМ. На целевой путь скачанное попадает
        // только после совпадения суммы: этот путь потом запускается, и
        // недокачанному файлу нельзя полежать на нём даже секунду.
        const auto partPath = target + QStringLiteral(".part");
        QFile::remove(partPath);
        {
            QFile part(partPath);
            if (!part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                return fail(QObject::tr("Не удалось записать файл на диск"));
            }
            const auto got = NetworkRequestHelper::HttpDownload(
                QUrl(kSite + kPath + platform + QStringLiteral("/file")), &part, wantSize, auth,
                progress);
            const bool flushed = part.flush();
            part.close();

            // Подписка проверяется сайтом и здесь заново — она могла кончиться
            // между двумя запросами. Разделение то же, что выше.
            // Тем же разбором, что и описание: подписка проверяется сайтом
            // заново, и между двумя запросами она могла кончиться. Своя копия
            // условий здесь однажды разошлась бы с той, что проверяется.
            if (got.status != 200) {
                QFile::remove(partPath);
                return InterpretMeta(got.status, {}, got.error).res;
            }
            if (!got.error.isEmpty() || !flushed) {
                QFile::remove(partPath);
                return fail(QObject::tr("Загрузка оборвалась. Повторите попытку"));
            }
        }

        const auto gotSha = sha256Of(partPath);
        if (gotSha != wantSha) {
            // Несовпадение — это НЕ повод оставить файл «на посмотреть».
            // Оставленный, он однажды будет запущен.
            QFile::remove(partPath);
            return fail(QObject::tr("Скачанный файл повреждён. Повторите попытку"));
        }

        if (!makeRunnable(partPath)) {
            QFile::remove(partPath);
            return fail(QObject::tr("Не удалось подготовить компонент к запуску"));
        }

        // Замена существующего. Windows не переименовывает поверх, а на любой
        // системе старый файл может быть ЗАНЯТ — тем самым подключением,
        // которое сейчас работает. Это единственная причина отказа, которую
        // человек может устранить сам, поэтому она называется прямо.
        if (QFile::exists(target) && !QFile::remove(target)) {
            QFile::remove(partPath);
            return fail(QObject::tr("Сначала отключитесь: компонент сейчас занят"));
        }
        if (!QFile::rename(partPath, target)) {
            QFile::remove(partPath);
            return fail(QObject::tr("Не удалось установить компонент"));
        }

        registerPath(target);

        Result r;
        r.ok = true;
        r.detail = version.isEmpty() ? QObject::tr("Резервное подключение готово")
                                     : QObject::tr("Резервное подключение готово (версия %1)").arg(version);
        return r;
    }

} // namespace RelayComponent
