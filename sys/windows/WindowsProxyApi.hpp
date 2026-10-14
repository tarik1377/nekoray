#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wininet.h>
#include "main/WindowsProxyController.hpp"

namespace GreenRhythm::WindowsProxy {
static_assert(Direct == PROXY_TYPE_DIRECT && Manual == PROXY_TYPE_PROXY
              && Pac == PROXY_TYPE_AUTO_PROXY_URL && AutoDetect == PROXY_TYPE_AUTO_DETECT);
// Tests exercise the native structures and call order through this boundary;
// they never touch the user's LAN or connectoid settings.
struct WinInetFunctions {
    decltype(&InternetQueryOptionW) query = &InternetQueryOptionW;
    decltype(&InternetSetOptionW) set = &InternetSetOptionW;
    decltype(&GlobalFree) free = &GlobalFree;
    decltype(&GetLastError) lastError = &GetLastError;
};
class WinInetApi final : public Api {
public:
    explicit WinInetApi(WinInetFunctions functions = {}) : functions(functions) {}
    bool query(State &state, QString &error) override {
        DWORD code = ERROR_SUCCESS;
        if (queryOnce(INTERNET_PER_CONN_FLAGS_UI, state, code)) return true;
        // Microsoft requires FLAGS_UI on Windows 7+, and FLAGS for restoring.
        // Only an unsupported option permits the older query; an access or
        // transient failure must stay visible instead of weakening verification.
        if ((code == ERROR_INVALID_PARAMETER || code == ERROR_INTERNET_INVALID_OPTION)
            && queryOnce(INTERNET_PER_CONN_FLAGS, state, code)) return true;
        error = failed("InternetQueryOption(LAN)", code);
        return false;
    }
    bool write(const State &state, QString &error) override {
        INTERNET_PER_CONN_OPTIONW options[2]{};
        options[0].dwOption = INTERNET_PER_CONN_FLAGS;
        options[0].Value.dwValue = state.flags;
        options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
        options[1].Value.pszValue = const_cast<wchar_t *>(reinterpret_cast<const wchar_t *>(state.server.utf16()));
        INTERNET_PER_CONN_OPTION_LISTW list{};
        list.dwSize = sizeof(list);
        list.dwOptionCount = 2;
        list.pOptions = options;
        // Null connection means LAN only. Corporate RAS/connectoid proxy policy
        // is outside this action; enumeration cannot suppress notifications.
        if (functions.set(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, sizeof(list))) return true;
        error = failed("InternetSetOption(LAN)", functions.lastError());
        return false;
    }
    bool notify(QString &error) override {
        const bool changed = functions.set(nullptr, INTERNET_OPTION_SETTINGS_CHANGED, nullptr, 0);
        const DWORD changedCode = changed ? ERROR_SUCCESS : functions.lastError();
        const bool refreshed = functions.set(nullptr, INTERNET_OPTION_REFRESH, nullptr, 0);
        const DWORD refreshCode = refreshed ? ERROR_SUCCESS : functions.lastError();
        if (!changed) error = failed("InternetSetOption(SETTINGS_CHANGED)", changedCode);
        else if (!refreshed) error = failed("InternetSetOption(REFRESH)", refreshCode);
        return changed && refreshed;
    }
private:
    bool queryOnce(DWORD flagOption, State &state, DWORD &code) {
        INTERNET_PER_CONN_OPTIONW options[2]{};
        options[0].dwOption = flagOption;
        options[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
        INTERNET_PER_CONN_OPTION_LISTW list{};
        list.dwSize = sizeof(list);
        list.dwOptionCount = 2;
        list.pOptions = options;
        DWORD size = sizeof(list);
        const bool ok = functions.query(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size);
        code = ok ? ERROR_SUCCESS : functions.lastError();
        if (ok) {
            state.flags = options[0].Value.dwValue;
            state.server = options[1].Value.pszValue ? QString::fromWCharArray(options[1].Value.pszValue) : QString();
        }
        if (options[1].Value.pszValue) functions.free(options[1].Value.pszValue);
        return ok;
    }
    static QString failed(const char *operation, DWORD code) {
        return QStringLiteral("Windows: %1 завершился с ошибкой %2.").arg(QString::fromLatin1(operation)).arg(code);
    }
    WinInetFunctions functions;
};
}
