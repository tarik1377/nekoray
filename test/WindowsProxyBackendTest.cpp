#include "test/WindowsProxyFixture.hpp"
#include <QCoreApplication>
#include <cstdio>
using namespace ProxyTest;
static int failures = 0;
static void check(const char *name, bool ok) {
    failures += !ok; std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString local = "127.0.0.1:2080";
    const State foreign{Direct | Manual, "corp-proxy.example:8080"};
    const State pac{Direct | Pac | AutoDetect, ""};
    QString error;
    {
        NativeFixture f; WinInetApi api(f.functions()); Controller controller(api);
        check("LAN enable is verified", controller.enable(local, error) && same(f.state, {3, local}));
        check("native options are zero initialized LAN-only and notify before readback", f.validCalls
              && f.calls == QStringList{"query", "write", "changed", "refresh", "query"} && f.freed == f.queries
              && f.flagOptions == QList<DWORD>{INTERNET_PER_CONN_FLAGS_UI, INTERNET_PER_CONN_FLAGS_UI});
        f.state.flags = Direct;
        check("explicit reapply repairs external disable", controller.enable(local, error) && f.state.flags == 3 && f.writes == 2);
        check("exit restores externally disabled state", controller.disable(error) && f.state.flags == Direct && f.state.server == local);
        const auto writes = f.writes;
        check("repeat cleanup is a read/write-free no-op", controller.disable(error) && f.writes == writes);
    }
    {
        NativeFixture f; f.state = foreign; WinInetApi api(f.functions()); Controller controller(api);
        controller.enable(local, error); controller.enable("127.0.0.1:23080", error);
        check("endpoint reapply retains prior foreign setting", controller.disable(error) && same(f.state, foreign));
    }
    {
        NativeFixture f; f.state = {3, local}; WinInetApi api(f.functions()); Controller controller(api);
        controller.enable(local, error);
        check("stale own localhost proxy is disabled on normal exit", controller.disable(error) && f.state.flags == Direct);
    }
    for (const auto replacement : {foreign, pac, State{Direct | Manual | AutoDetect, local}}) {
        NativeFixture f; WinInetApi api(f.functions()); Controller controller(api);
        controller.enable(local, error); f.state = replacement;
        const auto writes = f.writes;
        check("cleanup preserves external endpoint or auto-configuration", controller.disable(error) && same(f.state, replacement) && f.writes == writes);
    }
    {
        NativeFixture f; f.state = pac; WinInetApi api(f.functions()); Controller controller(api);
        controller.enable(local, error);
        check("prior PAC/autodetection flags restored without changing PAC URL", controller.disable(error) && same(f.state, pac) && f.validCalls);
    }
    {
        NativeFixture f; f.state = foreign; f.failWrites = 1; f.partialWriteFailure = true;
        WinInetApi api(f.functions()); Controller controller(api);
        check("failed partial SET is reported and original state restored", !controller.enable(local, error) && same(f.state, foreign) && error.contains("5"));
        check("failed SET still triggers both notifications and verifies recovery", f.changed == 2 && f.refreshed == 2 && f.validCalls && f.freed == f.queries);
    }
    {
        NativeFixture f; f.state = foreign; f.ignoreWrite = true; WinInetApi api(f.functions()); Controller controller(api);
        check("SET success without readback match fails closed", !controller.enable(local, error) && same(f.state, foreign) && f.writes == 1 && !error.isEmpty());
    }
    for (const bool failRefresh : {false, true}) {
        NativeFixture f; f.state = foreign;
        if (failRefresh) f.failRefresh = 1; else f.failChanged = 1;
        WinInetApi api(f.functions()); Controller controller(api);
        check("notification errors are surfaced and both calls run", !controller.enable(local, error) && same(f.state, foreign)
              && f.changed == 2 && f.refreshed == 2 && error.contains("5"));
    }
    {
        NativeFixture f; f.failQueryAt = 1; WinInetApi api(f.functions()); Controller controller(api);
        check("failed initial query cannot mutate state and frees partial server", !controller.enable(local, error) && f.writes == 0 && f.freed == 1 && error.contains("Query"));
    }
    {
        NativeFixture f; f.uiUnsupported = true; WinInetApi api(f.functions()); Controller controller(api);
        check("unsupported UI flag query falls back to legacy and frees all allocations", controller.enable(local, error)
              && f.flagOptions == QList<DWORD>{INTERNET_PER_CONN_FLAGS_UI, INTERNET_PER_CONN_FLAGS,
                                              INTERNET_PER_CONN_FLAGS_UI, INTERNET_PER_CONN_FLAGS}
              && f.validCalls && f.freed == 4);
    }
    {
        NativeFixture f; f.state = foreign; f.failQueryAt = 2; WinInetApi api(f.functions()); Controller controller(api);
        check("readback query failure is surfaced and rechecked before recovery", !controller.enable(local, error) && same(f.state, foreign)
              && f.queries == 4 && f.freed == 4 && f.writes == 2);
    }
    {
        NativeFixture f; f.state = foreign; f.failChanged = 1;
        f.afterNotification = [&](DWORD option) { if (option == INTERNET_OPTION_REFRESH && f.refreshed == 1) f.failWrites = 1; };
        WinInetApi api(f.functions()); Controller controller(api);
        check("failed recovery retains ownership", !controller.enable(local, error) && f.state.server == local && error.contains("Не удалось вернуть"));
        f.afterNotification = {};
        check("explicit cleanup retries failed recovery", controller.disable(error) && same(f.state, foreign));
    }
    for (const auto replacement : {foreign, pac}) {
        NativeFixture f; // SET and notify succeed; readback exposes replacement.
        f.afterNotification = [&](DWORD option) { if (option == INTERNET_OPTION_REFRESH) f.state = replacement; };
        WinInetApi api(f.functions()); Controller controller(api);
        check("failed enable preserves a concurrently replaced foreign state", !controller.enable(local, error)
              && same(f.state, replacement) && f.writes == 1);
        check("cleanup cannot reclaim that external state", controller.disable(error) && same(f.state, replacement) && f.writes == 1);
    }
    {
        NativeFixture f; WinInetApi api(f.functions()); Controller controller(api);
        controller.enable(local, error); f.state = foreign;
        check("explicit reclaim after foreign replacement applies", controller.enable(local, error));
        check("reclaimed session restores the most recent foreign prior", controller.disable(error) && same(f.state, foreign));
    }
    return failures ? 1 : 0;
}
