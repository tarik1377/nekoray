#include "RelayTrace.hpp"

namespace RelayTrace {

    namespace {
        const QString kWarmMark = QStringLiteral("relay-milestone warm ");
        const QString kCarryMark = QStringLiteral("relay-milestone carry ");
        const QString kUdpMark = QStringLiteral("relay-milestone udp ");

        // Больше восемнадцати цифр не бывает ни у срока, ни у счётчика, а
        // длинная последовательность — признак испорченной строки, а не числа.
        // Разбирать её как число значит переполниться молча.
        constexpr int kMaxDigits = 18;

        /**
         * Жёсткий курсор: читает ровно то, что названо, и ничего вокруг.
         *
         * Ни QRegularExpression, ни split. Регулярное выражение здесь легко
         * написать так, что оно пропустит хвост, а хвост — это и есть тот
         * адрес, ради недопущения которого всё затевалось.
         */
        class Scan {
        public:
            Scan(const QString &text, int at) : text_(text), at_(at) {}

            bool word(const QString &expected) {
                // QStringView, а не text_.mid(): mid() копирует хвост строки на
                // каждое сравнение, а сравнений здесь пять на строку.
                if (!QStringView(text_).mid(at_).startsWith(expected)) return false;
                at_ += expected.length();
                return true;
            }

            /** Неотрицательное целое, либо -1, если на месте курсора его нет. */
            qint64 number() {
                const int from = at_;
                while (at_ < text_.length()) {
                    const QChar c = text_.at(at_);
                    if (c < QLatin1Char('0') || c > QLatin1Char('9')) break;
                    at_++;
                }
                const int digits = at_ - from;
                if (digits == 0 || digits > kMaxDigits) return -1;
                bool ok = false;
                const qint64 v = text_.mid(from, digits).toLongLong(&ok);
                return ok ? v : -1;
            }

            bool done() const { return at_ == text_.length(); }

        private:
            const QString &text_;
            int at_;
        };

        QString ms(qint64 v) { return QString::number(v); }

        /**
         * Миг, а не исход.
         *
         * «Первый» сказано намеренно: строка обязана читаться как «остальные
         * ещё идут», иначе поддержка примет её за приговор и станет искать
         * причину там, где ещё ничего не случилось.
         */
        QString carryText(qint64 took, qint64 limit) {
            return QStringLiteral("каналы: первый подтверждён за %1 мс (предел %2 мс)")
                .arg(ms(took), ms(limit));
        }

        /**
         * Пять разных исходов, и они действительно разные.
         *
         * «Не поднято ни одного» (of == 0) — хранилище не ответило вовсе, узел
         * ни при чём. «Не подтверждён ни один» (ok == 0) — каналы построены, но
         * ни один не отозвался. Первый случай ветка ok == of сложила бы как
         * «подтверждены все», то есть отчиталась бы об успехе; поэтому он
         * проверяется раньше.
         *
         * Срок и время названы числами: по ним видно, не успел узел или отказал,
         * а читающему журнал не нужно знать наших констант.
         */
        QString warmText(qint64 ok, qint64 of, qint64 took, qint64 limit) {
            if (of == 0) {
                return QStringLiteral("каналы: не поднято ни одного за %1 мс (предел %2 мс)")
                    .arg(ms(took), ms(limit));
            }
            if (ok == of) {
                return QStringLiteral("каналы: подтверждены все за %1 мс (предел %2 мс)")
                    .arg(ms(took), ms(limit));
            }
            if (ok == 0) {
                return took >= limit
                           ? QStringLiteral("каналы: не подтверждён ни один, предел %1 мс вышел (ждали %2 мс)")
                                 .arg(ms(limit), ms(took))
                           : QStringLiteral("каналы: не подтверждён ни один за %1 мс (предел %2 мс)")
                                 .arg(ms(took), ms(limit));
            }
            if (took >= limit) {
                return QStringLiteral("каналы: подтверждены не все, предел %1 мс вышел (ждали %2 мс)")
                    .arg(ms(limit), ms(took));
            }
            return QStringLiteral("каналы: подтверждены не все за %1 мс, остальные не поднялись (предел %2 мс)")
                .arg(ms(took), ms(limit));
        }

        /**
         * Состояние датаграмм — то, ради чего человек и открывает журнал, когда
         * «подключено», а позвонить нельзя.
         *
         * ЧЕТЫРЕ ИСХОДА, И ОНИ РАЗНЫЕ ПО ДЕЙСТВИЮ. «Датаграмм не было» — человек
         * ещё не звонил, и жаловаться не на что. «Идут напрямую» — переключатель
         * включён и работает; если разговор всё равно не выходит, режут в самой
         * сети, и дальше искать надо там. «Не идут» — переключатель выключен, и
         * это лечится им же. Смешанное состояние называется отдельно: часть
         * ушла, часть нет, и обе цифры нужны, иначе поддержка увидит «идут» и
         * закроет обращение.
         *
         * Порт назван числом: по нему видно, чего человек лишился (3478 — это
         * разговор), и не видно, с кем он разговаривал.
         */
        QString udpText(qint64 direct, qint64 dropped, qint64 port) {
            if (direct == 0 && dropped == 0) {
                return QStringLiteral("звонки и игры: датаграмм не было");
            }
            if (dropped == 0) {
                return QStringLiteral("звонки и игры: идут напрямую (%1 шт., порт %2)")
                    .arg(ms(direct), ms(port));
            }
            if (direct == 0) {
                return QStringLiteral("звонки и игры: не идут, отброшено %1 (порт %2) — "
                                      "включите «Звонки и игры напрямую» в профиле")
                    .arg(ms(dropped), ms(port));
            }
            return QStringLiteral("звонки и игры: часть идёт напрямую (%1), часть отброшена (%2), порт %3")
                .arg(ms(direct), ms(dropped), ms(port));
        }

        QString scanUdp(const QString &line) {
            const int mark = line.indexOf(kUdpMark);
            if (mark < 0) return {};

            Scan s(line, mark + kUdpMark.length());
            if (!s.word(QStringLiteral("direct="))) return {};
            const qint64 direct = s.number();
            if (!s.word(QStringLiteral(" dropped="))) return {};
            const qint64 dropped = s.number();
            if (!s.word(QStringLiteral(" port="))) return {};
            const qint64 port = s.number();
            // Хвоста быть не должно — по той же причине, что и у соседок.
            if (!s.done()) return {};
            if (direct < 0 || dropped < 0 || port < 0) return {};
            // Номера порта выше этого не существует; большее число означает
            // испорченную строку, а не редкий случай.
            if (port > 65535) return {};

            return udpText(direct, dropped, port);
        }

        QString scanCarry(const QString &line) {
            const int mark = line.indexOf(kCarryMark);
            if (mark < 0) return {};

            Scan s(line, mark + kCarryMark.length());
            if (!s.word(QStringLiteral("took="))) return {};
            const qint64 took = s.number();
            if (!s.word(QStringLiteral("ms limit="))) return {};
            const qint64 limit = s.number();
            if (!s.word(QStringLiteral("ms"))) return {};
            // Хвоста быть не должно. Именно эта строчка не пускает в журнал
            // адрес, приписанный за форматом.
            if (!s.done()) return {};
            if (took < 0 || limit < 0) return {};

            return carryText(took, limit);
        }

        QString scanWarm(const QString &line) {
            const int mark = line.indexOf(kWarmMark);
            if (mark < 0) return {};

            Scan s(line, mark + kWarmMark.length());
            if (!s.word(QStringLiteral("ok="))) return {};
            const qint64 ok = s.number();
            if (!s.word(QStringLiteral(" of="))) return {};
            const qint64 of = s.number();
            if (!s.word(QStringLiteral(" took="))) return {};
            const qint64 took = s.number();
            if (!s.word(QStringLiteral("ms limit="))) return {};
            const qint64 limit = s.number();
            if (!s.word(QStringLiteral("ms"))) return {};
            if (!s.done()) return {};
            if (ok < 0 || of < 0 || took < 0 || limit < 0) return {};
            // Подтверждённых больше построенных не бывает; такая пара означает
            // испорченную строку, а не удачу.
            if (ok > of) return {};

            return warmText(ok, of, took, limit);
        }
    } // namespace

    QString Line(const QString &engineLine) {
        const QString line = engineLine.trimmed();
        if (line.isEmpty()) return {};

        const QString carry = scanCarry(line);
        if (!carry.isEmpty()) return carry;

        const QString udp = scanUdp(line);
        if (!udp.isEmpty()) return udp;

        return scanWarm(line);
    }

} // namespace RelayTrace
