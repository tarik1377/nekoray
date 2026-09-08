#include "TunHelper.hpp"

#include <QFile>
#include <QObject>
#include <QRegularExpression>

namespace GreenRhythm::TunHelper {

    namespace {
        // Имена файлов — одно место здесь и одно в скрипте; набор сверяет их.
        const QString kPidFile = QStringLiteral("tun-helper.pid");
        const QString kLogFile = QStringLiteral("tun-helper.log");
        // Адрес из шаблона. Меняется в двух местах сразу, набор про это знает.
        const QString kOurAddress = QStringLiteral("172.19.0.1");

        QString slurp(const QString &path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) return {};
            return QString::fromUtf8(f.readAll());
        }
    } // namespace

    QString pidPath(const QString &configDir) { return configDir + "/" + kPidFile; }
    QString logPath(const QString &configDir) { return configDir + "/" + kLogFile; }
    QString ourAddress() { return kOurAddress; }

    qint64 parsePid(const QString &text) {
        bool ok = false;
        const auto v = text.trimmed().toLongLong(&ok);
        return ok && v > 0 ? v : 0;
    }

    qint64 readPid(const QString &configDir) {
        return parsePid(slurp(pidPath(configDir)));
    }

    qint64 readNewLines(const QString &path, qint64 offset, QStringList &out) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return offset;
        if (f.size() < offset) offset = 0; // файл начат заново
        if (!f.seek(offset)) return offset;
        const auto data = f.readAll();
        const int lastNl = data.lastIndexOf('\n');
        if (lastNl < 0) return offset;
        const auto complete = QString::fromUtf8(data.constData(), lastNl);
        for (const auto &line: complete.split('\n')) {
            const auto t = line.trimmed();
            if (!t.isEmpty()) out << t;
        }
        return offset + lastNl + 1;
    }

    QString lastLines(const QString &path, int n) {
        const auto text = slurp(path);
        if (text.isEmpty()) return {};
        QStringList lines;
        for (const auto &line: text.split('\n')) {
            const auto t = line.trimmed();
            if (!t.isEmpty()) lines << t;
        }
        if (lines.size() > n) lines = lines.mid(lines.size() - n);
        return lines.join('\n');
    }

    QString routeInterface(const QString &routeGetOutput) {
        static const QRegularExpression re(QStringLiteral("^\\s*interface:\\s*(\\S+)"),
                                           QRegularExpression::MultilineOption);
        const auto m = re.match(routeGetOutput);
        return m.hasMatch() ? m.captured(1) : QString();
    }

    QString interfaceWithAddress(const QString &ifconfigOutput, const QString &addr) {
        // Имя интерфейса стоит в начале строки без отступа («utun4: flags=…»),
        // адреса — с отступом ниже. Ищем адрес целым словом: «172.19.0.1» не
        // должен находиться внутри «172.19.0.10».
        const QRegularExpression word(QStringLiteral("(^|\\s)") + QRegularExpression::escape(addr) + QStringLiteral("(\\s|$)"));
        QString current;
        for (const auto &ln: ifconfigOutput.split('\n')) {
            if (!ln.startsWith(' ') && !ln.startsWith('\t')) current = ln.section(':', 0, 0).trimmed();
            if (!current.isEmpty() && word.match(ln).hasMatch()) return current;
        }
        return {};
    }

    QString shellSingleQuote(const QString &s) {
        return "'" + QString(s).replace("'", QStringLiteral("'\\''")) + "'";
    }

    QString appleScriptLiteral(const QString &s) {
        return QString(s).replace("\\", "\\\\").replace("\"", "\\\"");
    }

    QString macStartCommand(const QString &scriptPath) {
        return appleScriptLiteral("bash " + shellSingleQuote(scriptPath));
    }

    QString macStopCommand(qint64 helperPid, qint64 mainCorePid) {
        const auto sweep = QStringLiteral("for p in $(pgrep -U 0 greenrhythm_core); do "
                                          "if [ $p != %1 ]; then kill -2 $p || exit 1; fi; done")
                               .arg(mainCorePid);
        if (helperPid <= 0) return sweep;
        // Имя под этим номером — ядро? Тогда сигнал ему. Нет — номер остался от
        // прошлого запуска и достался кому-то другому: идём обходом.
        return QStringLiteral("p=%1; case $(ps -p $p -o comm= 2>/dev/null) in "
                              "*greenrhythm_core) kill -2 $p ;; *) %2 ;; esac")
            .arg(helperPid)
            .arg(sweep);
    }

    bool looksCancelled(const QString &osascriptOutput) {
        // Код ищется В КОНЦЕ строки, а не подстрокой. Отказ скрипта приносит
        // сюда весь его след (set -x): пути, echo, коды возврата, — и «-128»
        // где-то в середине превратило бы настоящую поломку в спокойное
        // «вы отменили пароль».
        static const QRegularExpression code(QStringLiteral("\\(-128\\)\\s*$"));
        const auto text = osascriptOutput.trimmed();
        return code.match(text).hasMatch() ||
               text.contains(QStringLiteral("User canceled"), Qt::CaseInsensitive);
    }

    bool looksCancelledExit(int exitCode, const QString &output) {
        // pkexec на Linux: отменённое окно polkit — код 126 и «Not authorized».
        // Разные оболочки прав, признак один — человек не подтвердил.
        return looksCancelled(output) || exitCode == 126 ||
               output.contains(QStringLiteral("Not authorized"), Qt::CaseInsensitive);
    }

    QString firstBrokenLink(const Chain &c, const QString &socksAddr, int socksPort) {
        if (!c.helperAlive) {
            return QObject::tr("Помощник туннеля не запущен: процесса с номером из файла нет.");
        }
        if (c.adapter.isEmpty()) {
            return QObject::tr("Устройство туннеля не поднялось: адреса %1 нет ни на одном интерфейсе.").arg(kOurAddress);
        }
        if (c.routeVia != c.adapter) {
            return QObject::tr("Маршруты не установились: путь к 1.1.1.1 идёт через %1, а не через устройство туннеля %2.")
                .arg(c.routeVia.isEmpty() ? QStringLiteral("—") : c.routeVia, c.adapter);
        }
        if (!c.socksOpen) {
            return QObject::tr("Основное ядро не слушает %1:%2 — трафику из туннеля некуда идти.").arg(socksAddr).arg(socksPort);
        }
        if (!c.throughTunnel) {
            return QObject::tr("Все звенья на месте, но ответ из сети не приходит: цепочка помощник → "
                               "SOCKS основного ядра → сервер где-то обрывается.");
        }
        return {};
    }

} // namespace GreenRhythm::TunHelper
