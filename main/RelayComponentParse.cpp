#include "RelayComponent.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>

/**
 * Разбор ответов сайта — без сети и без диска, поэтому проверяемый.
 *
 * Вынесено по образцу RelayActivationParse.cpp и ровно по той же причине: цена
 * ошибки здесь не в отказе, а в НЕВЕРНОМ СОВЕТЕ. Перепутанные 402 и 403
 * отправят человека с действующей подпиской платить второй раз, и он заплатит,
 * потому что так написало приложение.
 */
namespace RelayComponent {

    QString PlatformId() {
        // Строки те же, что в RELEASE_PLATFORMS на сайте. Менять только парой.
#ifdef Q_OS_WIN
        return QStringLiteral("windows-x64");
#elif defined(Q_OS_MACOS)
        // Архитектура именно СБОРКИ, а не машины. На Apple Silicon сборка под
        // Intel идёт через Rosetta, и машина честно ответит «arm64» — а
        // подложить рядом с ней arm64-компонент значит получить отказ запуска
        // в паре, где каждый по отдельности исправен.
    #if defined(__aarch64__) || defined(__arm64__)
        return QStringLiteral("macos-arm64");
    #else
        return QStringLiteral("macos-amd64");
    #endif
#elif defined(Q_OS_LINUX)
        return QStringLiteral("linux-x64");
#else
        return {};
#endif
    }

    Meta InterpretMeta(int status, const QByteArray &body, const QString &networkError) {
        Meta m;

        // Сеть проверяется ПЕРВОЙ. При обрыве status равен нулю, и любая
        // проверка по коду ниже приняла бы «не дошли» за осмысленный ответ.
        if (!networkError.isEmpty() && status == 0) {
            m.res.detail = QObject::tr("Не удалось связаться с сайтом. Проверьте интернет и повторите");
            return m;
        }

        switch (status) {
            case 402:
                // Подписка кончилась — лечится продлением.
                m.res.detail = QObject::tr("Резервное подключение входит в действующую подписку");
                m.res.needsSubscription = true;
                return m;
            case 403:
                // Канал закрыт распоряжением владельца, подписка при этом
                // действующая. Предлагать продление здесь — вредный совет.
                m.res.detail = QObject::tr("Резервное подключение пока недоступно. Мы его готовим");
                return m;
            case 401:
                m.res.detail = QObject::tr("Введите код из личного кабинета заново");
                m.res.needsSubscription = true;
                return m;
            case 204:
                // Платформа известна, компонента под неё пока не выложили. Это
                // не вина человека и не его дело чинить.
                m.res.detail = QObject::tr("Для этой системы резерв ещё готовится");
                return m;
            case 200:
                break;
            default:
                m.res.detail = QObject::tr("Не удалось связаться с сайтом. Проверьте интернет и повторите");
                return m;
        }

        const auto obj = QJsonDocument::fromJson(body).object();
        m.version = obj["version"].toString();
        m.sha256 = obj["sha256"].toString().toLower();
        m.sizeBytes = static_cast<qint64>(obj["sizeBytes"].toDouble());

        // НЕПОЛНОЕ ОПИСАНИЕ — ОТКАЗ, а не «скачаем и посмотрим». Без суммы
        // проверять скачанное нечем, и файл, который потом ЗАПУСКАЕТСЯ,
        // пришлось бы принять на веру. Длина проверяется ровная: строка короче
        // или длиннее шестидесяти четырёх — это не сумма, чем бы она ни была.
        if (m.sha256.size() != 64 || m.sizeBytes <= 0) {
            m.res.detail = QObject::tr("Сайт ответил непонятно. Повторите позже");
            return m;
        }
        // Сумма из шестнадцатеричных цифр и ничего кроме. Иначе строка вида
        // «ошибка» нужной длины прошла бы дальше и сравнилась бы с настоящей
        // суммой уже после того, как мегабайты скачаны.
        for (const auto c: m.sha256) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                m.res.detail = QObject::tr("Сайт ответил непонятно. Повторите позже");
                return m;
            }
        }

        m.res.ok = true;
        return m;
    }

} // namespace RelayComponent
