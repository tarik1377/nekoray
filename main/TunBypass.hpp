#pragma once

#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace GreenRhythm::TunBypass {
inline bool parse(const QString &text, QStringList *entries, QString *error) {
    QStringList result;
    QSet<QString> seen;
    int line = 0, domains = 0;
    if (text.size() > 65536) { *error = QStringLiteral("Список слишком большой: допустимо 64 КБ."); return false; }
    for (auto value : text.split('\n')) {
        ++line; value = value.trimmed();
        if (value.isEmpty() || value.startsWith('#')) continue;
        auto fail = [&] { *error = QStringLiteral("Строка %1: укажите домен шлюза, IP или сеть CIDR без масок и учётных данных.").arg(line); return false; };
        if (value.contains("://")) {
            const QUrl url(value, QUrl::StrictMode);
            if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || !url.userInfo().isEmpty() || url.host().isEmpty()) return fail();
            value = url.host();
        }
        QHostAddress address;
        if (address.setAddress(value)) {
            if (!address.scopeId().isEmpty()) return fail();
            value = address.toString() + (address.protocol() == QAbstractSocket::IPv4Protocol ? "/32" : "/128");
        } else if (value.contains('/')) {
            const auto subnet = QHostAddress::parseSubnet(value);
            if (subnet.second < 0 || !subnet.first.scopeId().isEmpty()) return fail();
            value = subnet.first.toString() + "/" + QString::number(subnet.second);
        } else {
            if (value.endsWith('.')) value.chop(1);
            value = QString::fromLatin1(QUrl::toAce(value)).toLower();
            if (value.isEmpty() || value.size() > 253) return fail();
            const QRegularExpression label("^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$");
            for (const auto &part : value.split('.')) if (!label.match(part).hasMatch()) return fail();
            if (!seen.contains(value) && ++domains > 64) { *error = QStringLiteral("Допустимо до 64 доменов шлюзов."); return false; }
        }
        if (!seen.contains(value)) { result << value; seen.insert(value); }
        if (result.size() > 1024) { *error = QStringLiteral("Допустимо до 1024 исключений."); return false; }
    }
    *entries = result;
    return true;
}

inline bool saveSetting(const QString &path, const QString &expected, const QString &text, QString *error) {
    QStringList entries;
    if (!parse(text, &entries, error)) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) { *error = QStringLiteral("Не удалось прочитать настройки: ") + input.errorString(); return false; }
    const auto bytes = input.readAll(); input.close();
    QJsonParseError jsonError;
    const auto document = QJsonDocument::fromJson(bytes, &jsonError);
    if (!document.isObject() || jsonError.error != QJsonParseError::NoError) { *error = QStringLiteral("Файл настроек повреждён. Изменения не записаны."); return false; }
    auto settings = document.object();
    if ((settings.contains("vpn_route_exclude_extra") && !settings.value("vpn_route_exclude_extra").isString())
        || settings.value("vpn_route_exclude_extra").toString() != expected) {
        *error = QStringLiteral("Исключения уже изменились. Закройте окно и откройте его снова."); return false;
    }
    settings.insert("vpn_route_exclude_extra", text);
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    const auto updated = QJsonDocument(settings).toJson(QJsonDocument::Indented);
    if (!output.open(QIODevice::WriteOnly) || output.write(updated) != updated.size() || !output.commit()) {
        *error = QStringLiteral("Не удалось сохранить исключения: ") + output.errorString(); return false;
    }
    return true;
}
}
