#include "MacProxyController.hpp"

#include "main/NekoGui.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>

namespace NekoGui_sys::MacProxy {

    namespace {

        /** Файл снимка. Он же признак того, что прошлый раз кончился аварией. */
        const QString kBackupFile = QStringLiteral("macos_proxy_backup.json");

        /** Путь к networksetup абсолютный: PATH нам не принадлежит. */
        const QString kNetworkSetup = QStringLiteral("/usr/sbin/networksetup");

        /** Сколько ждать команду. Она местная и быстрая; зависание тут — сбой. */
        constexpr int kWaitMs = 10000;

        struct Ran {
            int code = -1;
            QString out;
            [[nodiscard]] bool ok() const { return code == 0; }
        };

        /**
         * Позвать networksetup.
         *
         * Аргументы передаются СПИСКОМ и никогда не склеиваются в строку для
         * оболочки. Имена сетевых служб на маке содержат пробелы («Wi-Fi»,
         * «Thunderbolt Bridge», локализованные варианты), и склеенная строка
         * разъехалась бы на первом же из них — причём не отказом, а обращением
         * не к той службе.
         */
        Ran run(const QStringList &args) {
            QProcess p;
            p.start(kNetworkSetup, args);
            if (!p.waitForFinished(kWaitMs)) {
                p.kill();
                return {};
            }
            Ran r;
            r.code = p.exitCode();
            r.out = QString::fromUtf8(p.readAllStandardOutput());
            return r;
        }

        QString backupPath() { return kBackupFile; }

        /** Состояние одной службы: включён ли автофайл и какой у него адрес. */
        QJsonObject readService(const QString &service) {
            QJsonObject o;
            o["service"] = service;

            // Автонастройка (то, что мы и будем менять).
            const auto auto_ = run({"-getautoproxyurl", service});
            if (auto_.ok()) {
                for (const auto &line: auto_.out.split('\n')) {
                    const auto t = line.trimmed();
                    if (t.startsWith("URL:")) o["autoUrl"] = t.mid(4).trimmed();
                    if (t.startsWith("Enabled:")) o["autoOn"] = t.mid(8).trimmed() == "Yes";
                }
            }
            return o;
        }

        /**
         * Применить к службе то, что записано в её снимке.
         *
         * Адрес ставится ДО состояния. Обратный порядок на мгновение включает
         * автонастройку с прежним, чужим адресом — и если между двумя командами
         * что-то случится, человек останется с включённым прокси, который не
         * его.
         */
        bool applyService(const QJsonObject &o) {
            const auto service = o["service"].toString();
            if (service.isEmpty()) return false;

            const auto url = o["autoUrl"].toString();
            bool ok = true;
            if (!url.isEmpty()) {
                ok = run({"-setautoproxyurl", service, url}).ok() && ok;
            }
            const bool on = o["autoOn"].toBool();
            ok = run({"-setautoproxystate", service, on ? "on" : "off"}).ok() && ok;

            // Пустой адрес при выключенном состоянии — обычное «не настроено».
            // Ставить пустую строку адресом не надо: networksetup её отвергает.
            return ok;
        }

        bool writeBackup(const QJsonArray &snapshot) {
            // QSaveFile: неудачная запись не должна оставить полуфайл, который
            // при следующем старте прочитается как снимок и «восстановит»
            // половину служб.
            QSaveFile f(backupPath());
            if (!f.open(QIODevice::WriteOnly)) return false;
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            const auto bytes = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
            if (f.write(bytes) != bytes.size()) {
                f.cancelWriting();
                return false;
            }
            // commit() доводит запись до диска и заменяет файл одним действием.
            // Именно на этом держится весь порядок: после него снимок ЕСТЬ, и
            // только после этого можно трогать систему.
            return f.commit();
        }

        QJsonArray readBackup() {
            QFile f(backupPath());
            if (!f.open(QIODevice::ReadOnly)) return {};
            const auto doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            return doc.isArray() ? doc.array() : QJsonArray();
        }

    } // namespace

    QStringList NetworkServices() {
        const auto r = run({"-listallnetworkservices"});
        if (!r.ok()) return {};

        QStringList out;
        const auto lines = r.out.split('\n');
        for (int i = 0; i < lines.size(); i++) {
            const auto line = lines[i].trimmed();
            if (line.isEmpty()) continue;
            // Первая строка — предупреждение про звёздочку, а не служба.
            if (i == 0 && line.startsWith("An asterisk")) continue;
            // Звёздочка означает выключенную службу: трогать её незачем, а
            // networksetup на неё ругается.
            if (line.startsWith('*')) continue;
            out << line;
        }
        return out;
    }

    bool Enable(const QString &pacUrl) {
        if (pacUrl.isEmpty()) return false;

        const auto services = NetworkServices();
        if (services.isEmpty()) return false;

        // СНИМОК ТОЛЬКО ОДИН РАЗ. Если файл уже есть, значит прокси включён
        // нами же, и это повторный вызов после смены правил. Перезаписать
        // снимок сейчас означало бы запомнить СВОИ настройки как «то, что было
        // до нас», и восстанавливать стало бы нечего.
        if (!QFile::exists(backupPath())) {
            QJsonArray snapshot;
            for (const auto &s: services) snapshot << readService(s);
            if (!writeBackup(snapshot)) return false;
        }

        bool ok = true;
        for (const auto &s: services) {
            // Адрес раньше состояния — см. applyService.
            ok = run({"-setautoproxyurl", s, pacUrl}).ok() && ok;
            ok = run({"-setautoproxystate", s, "on"}).ok() && ok;
        }
        return ok;
    }

    bool Disable() {
        const auto snapshot = readBackup();
        if (snapshot.isEmpty()) {
            // Снимка нет — значит и включали не мы. Гасить прокси «на всякий
            // случай» нельзя: у человека может стоять свой, и он его не
            // включал заново после нас.
            return true;
        }

        bool ok = true;
        for (const auto &v: snapshot) ok = applyService(v.toObject()) && ok;

        // Снимок убирается ТОЛЬКО после успешного восстановления. Иначе
        // неудачная попытка оставила бы человека с нашими настройками и без
        // единственного описания того, как было.
        if (ok) QFile::remove(backupPath());
        return ok;
    }

    void RestoreIfCrashed() {
        if (!QFile::exists(backupPath())) return;
        // Файл пережил прошлый запуск — значит тот кончился падением или
        // убийством процесса, и в системе до сих пор стоит наш адрес.
        Disable();
    }

} // namespace NekoGui_sys::MacProxy
