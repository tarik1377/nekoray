#include "main/SealedStore.hpp"
#include "sys/XorSeal.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QString>

#include <unistd.h>

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

    /**
     * Идентификатор машины. Источников несколько, от сильного к слабому.
     *
     * СНАЧАЛА БЫЛ ОДИН — /etc/machine-id, — и этого не хватало. Файл БЫВАЕТ
     * ПУСТЫМ: в контейнерах он создан нулевой длины, на части минимальных
     * установок не заполнен, и до первой загрузки systemd его тоже нет. Тогда
     * ключ выходил пустым, Seal возвращала пустоту, запись реквизитов молча
     * отвечала отказом — то есть активация не завершалась НИКОГДА и без единого
     * слова. Поймано сборкой в контейнере: набор упал ровно на этом.
     *
     * boot_id сюда НЕ добавлен намеренно, хотя он есть всегда: он меняется при
     * каждой загрузке, и запечатанное им переставало бы читаться после
     * перезагрузки. Это хуже, чем не сохранить: человек считал бы, что доступ
     * есть, и терял бы его каждое утро.
     *
     * Последний источник — домашний каталог и номер пользователя — держится не
     * на железе и стойкости не добавляет; он нужен ради того, чтобы приложение
     * РАБОТАЛО там, где остального нет. Свойство, ради которого слой заводился,
     * сохраняется и на нём: файл, скопированный на другую машину или к другому
     * пользователю, не распечатается.
     */
    QByteArray machineId() {
        for (const auto *path: {"/etc/machine-id", "/var/lib/dbus/machine-id",
                                "/sys/class/dmi/id/product_uuid"}) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) continue;
            const auto id = f.readAll().trimmed();
            if (!id.isEmpty()) return id;
        }

        const auto home = QDir::homePath().toUtf8();
        if (!home.isEmpty()) return home + ":" + QByteArray::number(qint64(::getuid()));
        return {};
    }

    QByteArray machineKey() {
        const auto id = machineId();
        if (id.isEmpty()) {
            // Сюда доходит только если не прочитался НИ ОДИН источник, включая
            // домашний каталог. Пустой ключ означает отказ запечатать, а не
            // «запечатаем нулями»: последнее выглядело бы как работающая защита
            // и не было бы ею.
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
        // Источник называется честно: если дошло до домашнего каталога, значит
        // machine-id не прочитался, и запечатка слабее обычного.
        return machineKey().isEmpty() ? QStringLiteral("недоступна")
                                      : QStringLiteral("machine-id (обфускация, не шифрование)");
    }

} // namespace SealedStore
