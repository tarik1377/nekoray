#pragma once

#include <QString>

// GreenRhythm / «Зелёный Ритм» service branding — single source of truth for the
// service URLs and Telegram handle used by the menu, About dialog and toolbar entry
// points. GreenRhythm stays a universal VPN client: these are opt-in default help /
// purchase entry points, never locks. (MSVC builds with /utf-8, cmake/windows/windows.cmake.)
namespace GreenRhythm {
    inline const QString kServiceName    = QStringLiteral("Зелёный Ритм");
    inline const QString kSiteUrl        = QStringLiteral("https://verdantvibe.ru");
    inline const QString kBuyUrl         = QStringLiteral("https://verdantvibe.ru/subscriptions");
    inline const QString kRenewUrl       = QStringLiteral("https://verdantvibe.ru/subscriptions/my");
    inline const QString kTelegramUrl    = QStringLiteral("https://t.me/VerdantVibeBot");
    inline const QString kTelegramHandle = QStringLiteral("@VerdantVibeBot");
} // namespace GreenRhythm
