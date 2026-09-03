#include "main/PortHealth.hpp"

#include <QObject>
#include <QRegularExpression>

namespace GreenRhythm {

    namespace {
        /**
         * Порог, после которого о резерве стоит говорить вслух.
         *
         * Пара сотен занятых номеров — обычная жизнь машины с Hyper-V, и пугать
         * ими незачем. Тысяча и больше означает, что система нарезала себе
         * десятки блоков по всему пространству, и попадание в чужой фиксированный
         * порт становится делом времени.
         */
        constexpr int kNoisyReservation = 1000;
    } // namespace

    int PortHealth::reservedPorts() const {
        int total = 0;
        for (const auto &r: reserved) total += r.to - r.from + 1;
        return total;
    }

    bool PortHealth::isReserved(int port) const {
        for (const auto &r: reserved) {
            if (r.covers(port)) return true;
        }
        return false;
    }

    bool PortHealth::worthReporting() const {
        return dynamicRangeWidened() || reservedPorts() >= kNoisyReservation;
    }

    QString PortHealth::verdict() const {
        if (!worthReporting()) return {};

        QString text = QObject::tr(
            "Windows держит за собой %1 сетевых номеров — их забрали Hyper-V, WSL или Docker.")
                           .arg(reservedPorts());

        if (dynamicRangeWidened()) {
            // Главное здесь — не число, а причина: пока диапазон расширен, резерв
            // может лечь на что угодно, и завтра сломается другая программа.
            text += QObject::tr(
                "\n\nПричина в том, что диапазон, из которого система их берёт, расширен: "
                "с %1 вместо обычного 49152. Из-за этого занятыми оказываются и те номера, "
                "на которые рассчитывают программы — игровые магазины, лаунчеры, античиты. "
                "Такая программа не жалуется на порт: она отвечает «ошибка соединения» или "
                "просто не запускается.")
                        .arg(dynamicFrom);
        }

        text += QObject::tr(
            "\n\nЭто чинится двумя командами в PowerShell от имени администратора:\n"
            "  netsh int ipv4 set dynamicport tcp start=49152 num=16384\n"
            "  netsh int ipv4 set dynamicport udp start=49152 num=16384\n"
            "затем «net stop winnat» и «net start winnat», либо перезагрузка.");
        return text;
    }

    PortHealth parsePortHealth(const QString &dynamicOutput, const QString &excludedOutput) {
        PortHealth h;

        // «Start Port : 1024» и «Number of Ports : 64511». Имена в локализованном
        // выводе переводятся, поэтому цепляемся за числа после двоеточия, а не за
        // слова: на русской Windows слова были бы другими, и разбор молча вернул
        // бы нули — то есть «всё в порядке» там, где не в порядке.
        static const QRegularExpression pair(QStringLiteral("^[^:\\n]*:\\s*(\\d+)\\s*$"),
                                             QRegularExpression::MultilineOption);
        QList<int> numbers;
        auto it = pair.globalMatch(dynamicOutput);
        while (it.hasNext()) numbers += it.next().captured(1).toInt();
        if (numbers.size() >= 2) {
            h.dynamicFrom = numbers[0];
            h.dynamicCount = numbers[1];
        }

        // Строки таблицы запретов: два числа подряд. Звёздочка в конце помечает
        // явную заявку приложения — для нас разницы нет, занят и занят.
        static const QRegularExpression range(QStringLiteral("^\\s*(\\d+)\\s+(\\d+)\\s*\\*?\\s*$"),
                                              QRegularExpression::MultilineOption);
        auto rit = range.globalMatch(excludedOutput);
        while (rit.hasNext()) {
            const auto m = rit.next();
            PortRange r{m.captured(1).toInt(), m.captured(2).toInt()};
            if (r.from > 0 && r.to >= r.from) h.reserved += r;
        }
        return h;
    }

} // namespace GreenRhythm
