#include "XorSeal.hpp"

#include <QCryptographicHash>

namespace {

    /**
     * Метка формата. Меняется, если меняется раскладка, — тогда старые файлы
     * честно перестанут читаться, а не прочитаются наполовину.
     */
    const QByteArray kMagic = QByteArrayLiteral("GRS1");

    /**
     * Сколько байт отпечатка хранить.
     *
     * Восемь: этого хватает, чтобы чужой или испорченный файл не прошёл
     * (вероятность случайного совпадения — одна на 18 миллионов миллиардов), и
     * при этом отпечаток не занимает половину файла. Задача у него одна —
     * ОТЛИЧИТЬ, а не защитить: подделать его может кто угодно, у кого есть
     * ключ, а у кого есть ключ — тому и подделывать незачем.
     */
    constexpr int kDigestBytes = 8;

    /** Отпечаток данных под этим ключом. */
    QByteArray digest(const QByteArray &plain, const QByteArray &key) {
        return QCryptographicHash::hash(key + plain, QCryptographicHash::Sha256)
            .left(kDigestBytes);
    }

    /**
     * Наложение потока. Ключ растягивается ХЕШИРОВАНИЕМ, а не повтором по
     * кругу: повтор 32-байтного ключа на длинном блобе даёт видимую
     * периодичность, по которой длина ключа читается с одного взгляда.
     */
    QByteArray stream(const QByteArray &data, const QByteArray &key) {
        QByteArray out(data.size(), Qt::Uninitialized);
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

namespace XorSeal {

    QByteArray Seal(const QByteArray &plain, const QByteArray &key) {
        if (plain.isEmpty() || key.isEmpty()) return {};
        return kMagic + digest(plain, key) + stream(plain, key);
    }

    QByteArray Unseal(const QByteArray &sealed, const QByteArray &key) {
        if (key.isEmpty()) return {};

        const int head = kMagic.size() + kDigestBytes;
        // Короче обрамления — точно не наше. Проверяется ДО разбора, иначе
        // срез ушёл бы за границу.
        if (sealed.size() <= head) return {};
        if (!sealed.startsWith(kMagic)) return {};

        const auto want = sealed.mid(kMagic.size(), kDigestBytes);
        const auto plain = stream(sealed.mid(head), key);

        // Отпечаток считается от РАСПЕЧАТАННОГО: так одна проверка отвечает
        // сразу на оба вопроса — тот ли ключ и целы ли данные.
        if (digest(plain, key) != want) return {};
        return plain;
    }

} // namespace XorSeal
