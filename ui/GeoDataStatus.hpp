#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace GreenRhythm::GeoData {
inline QString timestamp(const QJsonObject &status, const QString &key) {
    const auto date = QDateTime::fromString(status.value(key).toString(), Qt::ISODate);
    return date.isValid() && date.toSecsSinceEpoch() > 0
        ? date.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")) : QString();
}

inline QJsonObject readStatus(const QString &directory) {
    QFile file(QDir(directory).filePath(QStringLiteral(".geodata/status.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 16384) return {};
    const auto doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject();
}

inline QString describe(const QJsonObject &status, bool automatic, bool connected) {
    const auto state = status.value(QStringLiteral("state")).toString();
    if (connected && state == QStringLiteral("unused"))
        return QStringLiteral("Текущая схема не использует встроенную базу GeoIP РФ. "
                              "Автообновление применяется к схемам с geoip:ru.");
    if (state == QStringLiteral("unconfirmed"))
        return QStringLiteral("Ядро не подтвердило применение базы GeoIP. "
                              "Резервная копия сохранена. Повторите проверку обновления; "
                              "подтверждённого статуса действующих правил пока нет.");
    QStringList lines;
    if (!connected) lines << QStringLiteral("Проверка доступна при подключении к VPN.");
    else if (state == QStringLiteral("checking")) lines << QStringLiteral("Проверяем обновление GeoIP…");
    else if (state == QStringLiteral("error")) {
        lines << QStringLiteral("Не удалось обновить GeoIP. Сохранена последняя рабочая база.");
        const auto retry = timestamp(status, QStringLiteral("next_retry"));
        if (automatic && !retry.isEmpty()) lines << QStringLiteral("Повторная проверка: %1.").arg(retry);
    }
    const auto applied = timestamp(status, QStringLiteral("applied_at"));
    const auto checked = timestamp(status, QStringLiteral("checked_at"));
    if (!applied.isEmpty()) lines << QStringLiteral("Рабочая база применена: %1.").arg(applied);
    if (!checked.isEmpty()) lines << QStringLiteral("Последняя успешная проверка: %1.").arg(checked);
    const auto count = status.value(QStringLiteral("prefix_count")).toInt();
    if (count > 0 && count <= 250000) lines << QStringLiteral("Диапазонов РФ: %1.").arg(count);
    if (applied.isEmpty() && checked.isEmpty() && state != QStringLiteral("checking") && state != QStringLiteral("error"))
        lines << QStringLiteral("Используется встроенная база или последняя сохранённая копия.");
    lines << (automatic ? QStringLiteral("Автоматическая проверка — раз в сутки при работающем VPN.")
                        : QStringLiteral("Автообновление выключено; можно проверить вручную."));
    return lines.join(QChar('\n'));
}

inline QString brief(const QJsonObject &status, bool automatic, bool connected) {
    const auto state = status.value(QStringLiteral("state")).toString();
    if (state == QStringLiteral("unconfirmed"))
        return QStringLiteral("Ядро не подтвердило правила. Проверьте обновление ещё раз; резервная копия сохранена.");
    if (connected && state == QStringLiteral("unused"))
        return QStringLiteral("В этой схеме нет встроенного правила GeoIP РФ. Ваши правила сохранены.");
    QStringList lines;
    if (!connected) lines << QStringLiteral("Для проверки подключитесь к VPN.");
    else if (state == QStringLiteral("checking")) lines << QStringLiteral("Проверяем обновление базы…");
    else if (state == QStringLiteral("error")) lines << QStringLiteral("Обновить не удалось; действует предыдущая база.");
    const auto checked = timestamp(status, QStringLiteral("checked_at"));
    if (!checked.isEmpty()) {
        const auto count = status.value(QStringLiteral("prefix_count")).toInt();
        lines << (count > 0 && count <= 250000
            ? QStringLiteral("Проверено: %1 · диапазонов РФ: %2.").arg(checked).arg(count)
            : QStringLiteral("Последняя проверка: %1.").arg(checked));
    }
    else if (state != QStringLiteral("checking")) lines << QStringLiteral("Доступна встроенная база или сохранённая копия.");
    lines << (automatic ? QStringLiteral("Проверяется раз в сутки, без перезапуска туннеля.")
                        : QStringLiteral("Автопроверка выключена; можно проверить вручную."));
    return lines.join(QChar('\n'));
}

// A fixed, bounded request file is the only command accepted by the core. No
// URL, path or downloaded bytes from the GUI are interpreted by the updater.
inline bool requestRefresh(const QString &directory) {
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral(".geodata"))) return false;
    QSaveFile request(dir.filePath(QStringLiteral(".geodata/refresh.request")));
    const auto bytes = QUuid::createUuid().toByteArray(QUuid::WithoutBraces) + '\n';
    return request.open(QIODevice::WriteOnly) && request.write(bytes) == bytes.size() && request.commit();
}
inline bool saveAutomatic(const QString &directory, bool enabled) {
    QDir dir(directory);
    if (!dir.mkpath(QStringLiteral(".geodata"))) return false;
    QSaveFile settings(dir.filePath(QStringLiteral(".geodata/settings.json")));
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("enabled"), enabled}}).toJson(QJsonDocument::Compact);
    return settings.open(QIODevice::WriteOnly) && settings.write(bytes) == bytes.size() && settings.commit();
}
} // namespace GreenRhythm::GeoData
