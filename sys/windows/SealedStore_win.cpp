#include "main/SealedStore.hpp"

#include <QString>

#include <windows.h>
#include <dpapi.h>

/**
 * DPAPI, привязка к пользователю.
 *
 * CryptProtectData без CRYPTPROTECT_LOCAL_MACHINE шифрует ключом, выведенным из
 * учётной записи. Это ровно то свойство, которое нужно: скопированный на другую
 * машину или открытый под другим пользователем файл не расшифровывается, и
 * человек честно получает «активируйтесь заново» вместо чужих ключей.
 *
 * Дополнительная энтропия — не пароль и не секрет. Она лишь не даёт другой
 * программе того же пользователя распечатать наш блоб случайно, вызвав
 * CryptUnprotectData на подобранном файле: без той же строки вызов не пройдёт.
 * Прятать её негде и незачем — она в открытом коде и это не ослабление: вся
 * защита здесь в ключе пользователя, а не в этой строке.
 */
namespace {
    const char kEntropy[] = "GreenRhythm/relay-credentials/v1";

    DATA_BLOB entropyBlob() {
        DATA_BLOB b;
        b.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(kEntropy));
        b.cbData = sizeof(kEntropy) - 1;
        return b;
    }
} // namespace

namespace SealedStore {

    QByteArray Seal(const QByteArray &plain) {
        if (plain.isEmpty()) return {};

        DATA_BLOB in;
        in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
        in.cbData = static_cast<DWORD>(plain.size());

        DATA_BLOB entropy = entropyBlob();
        DATA_BLOB out{};
        if (!CryptProtectData(&in, L"GreenRhythm", &entropy, nullptr, nullptr, 0, &out)) {
            return {};
        }

        QByteArray sealed(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
        // Windows выделяет буфер сам; не освободить его — утечка на каждое
        // сохранение, а сохраняем мы при каждом обновлении ключей.
        LocalFree(out.pbData);
        return sealed;
    }

    QByteArray Unseal(const QByteArray &sealed) {
        if (sealed.isEmpty()) return {};

        DATA_BLOB in;
        in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(sealed.constData()));
        in.cbData = static_cast<DWORD>(sealed.size());

        DATA_BLOB entropy = entropyBlob();
        DATA_BLOB out{};
        if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, 0, &out)) {
            // Обычный случай, а не сбой: файл принесли с другой машины или из-под
            // другого пользователя. Ошибку наверх не несём — вызывающий и так
            // трактует пустоту как «реквизитов нет».
            return {};
        }

        QByteArray plain(reinterpret_cast<const char *>(out.pbData), static_cast<int>(out.cbData));
        // Затираем расшифрованное до освобождения: LocalFree память не чистит, и
        // ключи остались бы лежать в куче до следующего, кто её займёт.
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
        return plain;
    }

    QString Kind() { return QStringLiteral("DPAPI (учётная запись Windows)"); }

} // namespace SealedStore
