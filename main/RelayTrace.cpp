#include "RelayTrace.hpp"

namespace RelayTrace {

    namespace {
        const QString kWarmMark = QStringLiteral("relay-milestone warm ");
        const QString kCarryMark = QStringLiteral("relay-milestone carry ");

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

        return scanWarm(line);
    }

} // namespace RelayTrace
