#include "main/RunningPrograms.hpp"

#include <QProcess>
#include <QSet>

namespace GreenRhythm {

    QStringList runningPrograms() {
        QProcess p;
#ifdef Q_OS_WIN
        p.start(QStringLiteral("powershell"),
                {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                 QStringLiteral("-Command"),
                 QStringLiteral("Get-Process | ForEach-Object { $_.ProcessName } | Sort-Object -Unique")});
#else
        p.start(QStringLiteral("ps"), {QStringLiteral("-eo"), QStringLiteral("comm=")});
#endif
        if (!p.waitForFinished(10000)) {
            p.kill();
            return {};
        }

        QStringList names;
        QSet<QString> seen;
        const auto out = QString::fromLocal8Bit(p.readAllStandardOutput());
        for (const auto &raw: out.split(QChar('\n'))) {
            auto name = raw.trimmed();
            if (name.isEmpty()) continue;
#ifdef Q_OS_WIN
            if (!name.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
                name += QStringLiteral(".exe");
            }
#else
            name = name.section(QChar('/'), -1);
#endif
            if (seen.contains(name.toLower())) continue;
            seen.insert(name.toLower());
            names << name;
        }
        names.sort(Qt::CaseInsensitive);
        return names;
    }

} // namespace GreenRhythm
