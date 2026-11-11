#pragma once

#include <QString>

namespace GreenRhythm::StartupPolicy {
    inline constexpr int coreReadyMs = 60000;
#ifdef Q_OS_WIN
    inline constexpr int coreStartAttempts = 2;
#else
    inline constexpr int coreStartAttempts = 1;
#endif
    inline constexpr int profileStartMs = 60000;
    inline constexpr int maxStartAttempts = 3;
    inline constexpr int shellWatchdogMs = coreStartAttempts * coreReadyMs + maxStartAttempts * profileStartMs + 15000;

    enum class Retry { None, Port, AdapterReopen };

    inline bool isBindFailure(const QString &error) {
        if (!error.contains(QStringLiteral("bind"), Qt::CaseInsensitive)) return false;
        return error.contains(QStringLiteral("forbidden"), Qt::CaseInsensitive) ||
               error.contains(QStringLiteral("permissions"), Qt::CaseInsensitive) ||
               error.contains(QStringLiteral("already in use"), Qt::CaseInsensitive);
    }

    inline bool isAdapterReopenFailure(const QString &error) {
        // These prefixes come from sing-tun's create-then-open fallback, not
        // localized Win32 messages. A failed native call has already returned
        // and unwound before the same core receives the recovery request.
        return error.contains(QStringLiteral("inbound/tun["), Qt::CaseInsensitive) &&
               error.contains(QStringLiteral("configure tun interface:"), Qt::CaseInsensitive) &&
               error.contains(QStringLiteral("create adapter:"), Qt::CaseInsensitive) &&
               error.contains(QStringLiteral("| open existing adapter:"), Qt::CaseInsensitive);
    }

    inline Retry retryAfterError(const QString &error, bool adapterRecoveryUsed, bool windows) {
        if (isBindFailure(error)) return Retry::Port;
        if (windows && !adapterRecoveryUsed && isAdapterReopenFailure(error)) return Retry::AdapterReopen;
        return Retry::None;
    }
}
