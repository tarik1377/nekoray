#pragma once
#include "sys/windows/WindowsProxyApi.hpp"
#include <functional>

namespace ProxyTest {
using namespace GreenRhythm::WindowsProxy;
struct NativeFixture {
    State state;
    int queries = 0, writes = 0, changed = 0, refreshed = 0, freed = 0;
    int failWrites = 0, failQueryAt = 0, failChanged = 0, failRefresh = 0;
    bool partialWriteFailure = false, ignoreWrite = false, validCalls = true;
    bool uiUnsupported = false;
    DWORD errorCode = ERROR_ACCESS_DENIED;
    QList<DWORD> flagOptions;
    QStringList calls;
    std::function<void(DWORD)> afterNotification;
    static inline NativeFixture *active = nullptr;
    NativeFixture() { active = this; }
    WinInetFunctions functions() { return {query, set, free, lastError}; }
    static DWORD WINAPI lastError() { return active->errorCode; }
    static HGLOBAL WINAPI free(HGLOBAL memory) {
        ++active->freed; delete[] static_cast<wchar_t *>(memory); return nullptr;
    }
    static bool validList(HINTERNET handle, DWORD option, void *buffer, DWORD size, bool query = false) {
        auto *list = static_cast<INTERNET_PER_CONN_OPTION_LISTW *>(buffer);
        return handle == nullptr && option == INTERNET_OPTION_PER_CONNECTION_OPTION
            && size == sizeof(*list) && list && list->dwSize == sizeof(*list)
            && list->pszConnection == nullptr && list->dwOptionCount == 2 && list->dwOptionError == 0
            && list->pOptions && (list->pOptions[0].dwOption == INTERNET_PER_CONN_FLAGS
                || (query && list->pOptions[0].dwOption == INTERNET_PER_CONN_FLAGS_UI))
            && list->pOptions[1].dwOption == INTERNET_PER_CONN_PROXY_SERVER;
    }
    static BOOL WINAPI query(HINTERNET handle, DWORD option, void *buffer, DWORD *size) {
        auto &f = *active;
        ++f.queries; f.calls.append("query");
        f.validCalls &= size && validList(handle, option, buffer, *size, true);
        if (!f.validCalls) return FALSE;
        auto *list = static_cast<INTERNET_PER_CONN_OPTION_LISTW *>(buffer);
        f.flagOptions.append(list->pOptions[0].dwOption);
        f.validCalls &= list->pOptions[0].Value.dwValue == 0 && list->pOptions[1].Value.pszValue == nullptr;
        list->pOptions[0].Value.dwValue = f.state.flags;
        // Even a failed query can allocate a partially returned server. The
        // actual adapter must free it without exposing an unverified state.
        auto *server = new wchar_t[f.state.server.size() + 1];
        f.state.server.toWCharArray(server); server[f.state.server.size()] = L'\0';
        list->pOptions[1].Value.pszValue = server;
        const bool unsupported = f.uiUnsupported && list->pOptions[0].dwOption == INTERNET_PER_CONN_FLAGS_UI;
        f.errorCode = unsupported ? ERROR_INTERNET_INVALID_OPTION : ERROR_ACCESS_DENIED;
        return !unsupported && f.queries != f.failQueryAt;
    }
    static BOOL WINAPI set(HINTERNET handle, DWORD option, void *buffer, DWORD size) {
        auto &f = *active;
        if (option == INTERNET_OPTION_PER_CONNECTION_OPTION) {
            ++f.writes; f.calls.append("write");
            f.validCalls &= validList(handle, option, buffer, size);
            if (!f.validCalls) return FALSE;
            auto *list = static_cast<INTERNET_PER_CONN_OPTION_LISTW *>(buffer);
            const State wanted{list->pOptions[0].Value.dwValue,
                               QString::fromWCharArray(list->pOptions[1].Value.pszValue)};
            const bool fail = f.failWrites > 0;
            f.errorCode = ERROR_ACCESS_DENIED;
            if (fail) --f.failWrites;
            if (!f.ignoreWrite && (!fail || f.partialWriteFailure)) f.state = wanted;
            return !fail;
        }
        f.validCalls &= handle == nullptr && buffer == nullptr && size == 0;
        f.errorCode = ERROR_ACCESS_DENIED;
        bool fail = false;
        if (option == INTERNET_OPTION_SETTINGS_CHANGED) {
            ++f.changed; f.calls.append("changed");
            fail = f.failChanged > 0; if (fail) --f.failChanged;
        } else if (option == INTERNET_OPTION_REFRESH) {
            ++f.refreshed; f.calls.append("refresh");
            fail = f.failRefresh > 0; if (fail) --f.failRefresh;
        } else f.validCalls = false;
        if (f.afterNotification) f.afterNotification(option);
        return f.validCalls && !fail;
    }
};
}
