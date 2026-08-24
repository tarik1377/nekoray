#include "main/SealedStore.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QString>

/**
 * Linux: обфускация, а не защита. Здесь это сказано вслух намеренно.
 *
 * Нормальной запечатки уровня DPAPI в Linux нет: Secret Service есть не везде,
 * тянет DBus и в headless-сеансе не работает вовсе. Поэтому здесь — XOR
 * потоком, выведенным из machine-id, и права 0600 на самом файле (их ставит
 * DeviceCredentials).
 *
 * ЧТО ЭТО ДАЁТ. Ровно одно: скопированный на другую машину файл бесполезен, и
 * это тот случай, ради которого слой заводился. Плюс в отчёт поддержки, если
 * человек приложит файл, не попадут читаемые ключи.
 *
 * ЧЕГО НЕ ДАЁТ. Ничего от того, кто уже выполняет код на этой машине от имени
 * этого пользователя: machine-id читается всеми. Обещать здесь стойкость —
 * врать, а неверное обещание опаснее отсутствующего: на него полагаются.
 */
namespace {
    QByteArray machineKey() {
        for (const auto *path: {"/etc/machine-id", "/var/lib/dbus/machine-id"}) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const auto id = f.readAll().trimmed();
            if (id.isEmpty()) continue;
            return QCryptographicHash::hash(id + "GreenRhythm/relay-credentials/v1",
                                            QCryptographicHash::Sha256);
        }
        // machine-id нет — сдаёмся честно. Пустой ключ означает отказ
        // запечатать, а не «запечатаем нулями»: последнее выглядело бы как
        // работающая защита и не было бы ею.
        return {};
    }

    QByteArray xorStream(const QByteArray &data, const QByteArray &key) {
        if (key.isEmpty()) return {};
        QByteArray out(data.size(), Qt::Uninitialized);
        // Ключ растягивается хешированием, а не повторением по кругу: повтор
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

    QString Kind() { return QStringLiteral("machine-id (обфускация, не шифрование)"); }

} // namespace SealedStore
