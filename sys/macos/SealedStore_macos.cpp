#include "main/SealedStore.hpp"
#include "sys/XorSeal.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QProcess>
#include <QString>

#include <unistd.h>

/**
 * macOS: обфускация, а не защита, и это сказано вслух намеренно.
 *
 * ПОЧЕМУ ОТДЕЛЬНЫЙ ФАЙЛ, А НЕ ЛИНУКСОВЫЙ. Линуксовая запечатка выводит ключ из
 * /etc/machine-id. На маке такого файла нет вовсе — и это не «работало бы чуть
 * хуже»: ключ вышел бы ПУСТЫМ, Seal вернул бы пустоту, запись реквизитов
 * отвечала бы отказом, и активация на маке не завершалась бы никогда. Причём
 * молча: ни ошибки, ни строки в журнале, просто «сохранить не удалось».
 *
 * Ключ выводится из идентификатора машины, и источников у него несколько —
 * от сильного к слабому: IOPlatformUUID, серийный номер, kern.uuid, и в самом
 * конце домашний каталог с номером пользователя. Читаются они через ioreg и
 * sysctl, потому что иначе понадобилась бы IOKit и линковка фреймворка ради
 * одной строки.
 *
 * ЗАПАСНЫЕ ИСТОЧНИКИ — НЕ ПЕРЕСТРАХОВКА. Сначала он был один, и на виртуальной
 * машине не читался вовсе: ключ выходил пустым, запись реквизитов молча
 * отвечала отказом, и активация не завершалась никогда. Поймано первой же
 * сборкой на раннере.
 *
 * ЧТО ЭТО ДАЁТ. Ровно одно, и ради него слой и заводился: скопированный на
 * другую машину файл бесполезен. Плюс в отчёт поддержки, если человек приложит
 * файл, не попадут читаемые ключи.
 *
 * ЧЕГО НЕ ДАЁТ. Ничего от того, кто уже выполняет код на этой машине от имени
 * этого пользователя: IOPlatformUUID читает кто угодно. Обещать здесь стойкость
 * — врать, а неверное обещание опаснее отсутствующего: на него полагаются.
 *
 * ЧЕМ ЭТО ЗАМЕНИТЬ ПО-НАСТОЯЩЕМУ. Связкой ключей (Keychain): ключ хранился бы
 * вне досягаемости чужого процесса, и защита стала бы настоящей. Это требует
 * Security.framework и подписи приложения — до подписи заводить её нет смысла,
 * потому что неподписанному приложению система выдаёт отдельное хранилище,
 * теряемое при каждой пересборке.
 */
namespace {

    /** Значение поля из вывода ioreg. Строка вида:  "ИМЯ" = "ЗНАЧЕНИЕ" */
    QByteArray fromIoreg(const QString &out, const QString &field) {
        for (const auto &line: out.split('\n')) {
            if (!line.contains(field)) continue;
            const auto parts = line.split('"');
            if (parts.size() < 4) continue;
            const auto v = parts[3].trimmed();
            if (!v.isEmpty()) return v.toUtf8();
        }
        return {};
    }

    /** Одно значение sysctl. Пусто — не прочиталось. */
    QByteArray fromSysctl(const QString &name) {
        QProcess p;
        p.start("/usr/sbin/sysctl", {"-n", name});
        if (!p.waitForFinished(5000)) {
            p.kill();
            return {};
        }
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed().toUtf8();
    }

    /*
     * ИСТОЧНИКОВ НЕСКОЛЬКО, И ЭТО НЕ ПЕРЕСТРАХОВКА.
     *
     * Сначала был один — IOPlatformUUID из ioreg. На виртуальной машине его
     * может не оказаться, и тогда ключ выходил пустым, Seal возвращала пустоту,
     * запись реквизитов отвечала отказом — то есть активация не завершалась
     * никогда и молча. Поймано сборкой на раннере: credentials_test падал ровно
     * на этом, и это первое, что показала маковая сборка вообще.
     *
     * Порядок от сильного к слабому. Последний источник — домашний каталог и
     * номер пользователя — держится не на железе и стойкости не добавляет
     * вовсе; он нужен ради того, чтобы приложение РАБОТАЛО там, где остальное
     * не читается. Свойство, ради которого слой заводился, сохраняется и на
     * нём: файл, скопированный на другую машину или к другому пользователю, не
     * распечатается.
     */
    QByteArray platformUuid() {
        static QByteArray cached;
        static bool asked = false;
        if (asked) return cached;
        asked = true;

        QProcess p;
        p.start("/usr/sbin/ioreg", {"-rd1", "-c", "IOPlatformExpertDevice"});
        if (p.waitForFinished(5000)) {
            const auto out = QString::fromUtf8(p.readAllStandardOutput());
            cached = fromIoreg(out, QStringLiteral("IOPlatformUUID"));
            if (cached.isEmpty()) cached = fromIoreg(out, QStringLiteral("IOPlatformSerialNumber"));
        } else {
            p.kill();
        }

        if (cached.isEmpty()) cached = fromSysctl(QStringLiteral("kern.uuid"));

        if (cached.isEmpty()) {
            // Последний рубеж, и он назван слабым в шапке выше. Без него
            // приложение просто не работало бы там, где железо не опрашивается.
            const auto home = QDir::homePath().toUtf8();
            if (!home.isEmpty()) cached = home + ":" + QByteArray::number(qint64(::getuid()));
        }
        return cached;
    }

    QByteArray machineKey() {
        const auto id = platformUuid();
        if (id.isEmpty()) {
            // Сюда доходит только если не прочитался НИ ОДИН источник, включая
            // домашний каталог, — то есть система в состоянии, в котором
            // приложению всё равно нечего делать. Пустой ключ означает отказ
            // запечатать, а не «запечатаем нулями»: последнее выглядело бы как
            // работающая защита и не было бы ею.
            return {};
        }
        return QCryptographicHash::hash(id + "GreenRhythm/relay-credentials/v1",
                                        QCryptographicHash::Sha256);
    }

} // namespace

namespace SealedStore {

    QByteArray Seal(const QByteArray &plain) { return XorSeal::Seal(plain, machineKey()); }

    QByteArray Unseal(const QByteArray &sealed) { return XorSeal::Unseal(sealed, machineKey()); }

    QString Kind() {
        // Источник называется честно: если дошло до последнего рубежа, значит
        // железо не опрашивается, и запечатка слабее обычного.
        return machineKey().isEmpty() ? QStringLiteral("недоступна")
                                      : QStringLiteral("идентификатор машины (обфускация)");
    }

} // namespace SealedStore
