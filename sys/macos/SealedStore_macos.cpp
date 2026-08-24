#include "main/SealedStore.hpp"

#include <QCryptographicHash>
#include <QProcess>
#include <QString>

/**
 * macOS: обфускация, а не защита, и это сказано вслух намеренно.
 *
 * ПОЧЕМУ ОТДЕЛЬНЫЙ ФАЙЛ, А НЕ ЛИНУКСОВЫЙ. Линуксовая запечатка выводит ключ из
 * /etc/machine-id. На маке такого файла нет вовсе — и это не «работало бы чуть
 * хуже»: ключ вышел бы ПУСТЫМ, Seal вернул бы пустоту, запись реквизитов
 * отвечала бы отказом, и активация на маке не завершалась бы никогда. Причём
 * молча: ни ошибки, ни строки в журнале, просто «сохранить не удалось».
 *
 * Ключ здесь выводится из IOPlatformUUID — идентификатора машины, который
 * система заводит один раз и который переживает обновления. Читается он через
 * ioreg, потому что иначе понадобилась бы IOKit и линковка фреймворка ради
 * одной строки.
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

    /**
     * Идентификатор машины. Спрашивается один раз за запуск.
     *
     * Кэш здесь не про скорость: ioreg — внешний процесс, и звать его на каждую
     * запись означало бы ставить сохранение реквизитов в зависимость от того,
     * ответила ли посторонняя программа.
     */
    QByteArray platformUuid() {
        static QByteArray cached;
        static bool asked = false;
        if (asked) return cached;
        asked = true;

        QProcess p;
        p.start("/usr/sbin/ioreg", {"-rd1", "-c", "IOPlatformExpertDevice"});
        if (!p.waitForFinished(5000)) {
            p.kill();
            return cached;
        }
        const auto out = QString::fromUtf8(p.readAllStandardOutput());
        for (const auto &line: out.split('\n')) {
            if (!line.contains("IOPlatformUUID")) continue;
            // Строка вида:  "IOPlatformUUID" = "XXXXXXXX-...."
            const auto parts = line.split('"');
            if (parts.size() < 4) continue;
            cached = parts[3].trimmed().toUtf8();
            break;
        }
        return cached;
    }

    QByteArray machineKey() {
        const auto id = platformUuid();
        if (id.isEmpty()) {
            // Идентификатор не прочитался — сдаёмся честно. Пустой ключ означает
            // отказ запечатать, а не «запечатаем нулями»: последнее выглядело бы
            // как работающая защита и не было бы ею.
            return {};
        }
        return QCryptographicHash::hash(id + "GreenRhythm/relay-credentials/v1",
                                        QCryptographicHash::Sha256);
    }

    /** Тот же поток, что и на Linux: ключ растягивается хешированием. */
    QByteArray xorStream(const QByteArray &data, const QByteArray &key) {
        if (key.isEmpty()) return {};
        QByteArray out(data.size(), Qt::Uninitialized);
        // Растягивается хешированием, а не повтором по кругу: повтор
        // 32-байтного ключа на длинном блобе даёт видимую периодичность.
        QByteArray block = key;
        int at = 0;
        while (at < data.size()) {
            block = QCryptographicHash::hash(block, QCryptographicHash::Sha256);
            const int take = qMin(block.size(), data.size() - at);
            for (int i = 0; i < take; i++) out[at + i] = data[at + i] ^ block[i];
            at += take;
        }
        return out;
    }

} // namespace

namespace SealedStore {

    QByteArray Seal(const QByteArray &plain) {
        if (plain.isEmpty()) return {};
        return xorStream(plain, machineKey());
    }

    QByteArray Unseal(const QByteArray &sealed) {
        if (sealed.isEmpty()) return {};
        return xorStream(sealed, machineKey());
    }

    QString Kind() { return QStringLiteral("идентификатор машины (обфускация)"); }

} // namespace SealedStore
