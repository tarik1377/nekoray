#include "main/ProgramTrouble.hpp"

namespace GreenRhythm {

    namespace {

        /**
         * Неизменный признак соединения.
         *
         * Из полей, что присылает ядро, постоянны только эти четыре: адрес
         * назначения, сеть, программа и время начала. Порядковый номер для этого
         * не годится — он выдаётся заново на каждый опрос, и по нему одно
         * соединение выглядит десятком разных.
         */
        QString keyOf(const Seen &s) {
            return s.process.toLower() + QChar('\x1f') + s.network + QChar('\x1f') + s.dest +
                   QChar('\x1f') + QString::number(s.start);
        }

        bool goesOutside(const QString &tag) {
            return tag == QStringLiteral("direct") || tag == QStringLiteral("bypass");
        }

    } // namespace

    void Watch::add(const QList<Seen> &batch) {
        for (const auto &s: batch) {
            const auto key = keyOf(s);
            if (seenKeys.contains(key)) continue;
            seenKeys.insert(key);
            records += s;
        }
    }

    void Watch::clear() {
        records.clear();
        seenKeys.clear();
    }

    Finding Watch::finish(const QString &program) const {
        Finding f;
        f.name = program;

        QSet<QString> others;
        for (const auto &s: records) {
            const bool mine = s.process.compare(program, Qt::CaseInsensitive) == 0;
            if (!mine) {
                // Соседи по несчастью нужны для одного случая, зато частого:
                // человек называет лаунчер, а в туннель ходит сама игра. Без этой
                // подсказки он получает «причина не в нас» и уходит ни с чем.
                if (s.tag == QStringLiteral("proxy")) others.insert(s.process);
                continue;
            }
            if (s.tag == QStringLiteral("proxy")) {
                f.viaTunnel++;
                if (s.network == QStringLiteral("udp")) f.udpViaTunnel++;
            } else if (goesOutside(s.tag)) {
                f.direct++;
            }
        }

        f.companions = others.values();
        f.companions.sort(Qt::CaseInsensitive);

        if (f.viaTunnel == 0 && f.direct == 0) {
            f.verdict = Verdict::NotSeen;
        } else if (f.viaTunnel == 0) {
            f.verdict = Verdict::Direct;
        } else if (f.direct == 0) {
            f.verdict = Verdict::ThroughTunnel;
        } else {
            f.verdict = Verdict::Mixed;
        }
        return f;
    }

} // namespace GreenRhythm
