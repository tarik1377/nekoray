#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <cstdio>

namespace NekoGui {
struct FixtureStore {
    bool spmode_system_proxy = false;
    bool remember_enable = true;
    int inbound_socks_port = 2080;
    QStringList remember_spmode;
    int saves = 0;
    void Save() { ++saves; }
};
inline FixtureStore *dataStore = nullptr;
}
// The actual controller method is compiled below. Only its OS backend and
// settings store are substituted; this test cannot write WinINET or restart VPN.
struct FixtureOS {
    bool enabled = false;
    int httpPort = 0, socksPort = 0;
    int setCalls = 0, clearCalls = 0;
} os;
void SetSystemProxy(int http, int socks) {
    ++os.setCalls; os.enabled = true; os.httpPort = http; os.socksPort = socks;
}
void ClearSystemProxy() { ++os.clearCalls; os.enabled = false; }
class MainWindow : public QObject {
public:
    int refreshes = 0;
    void refresh_status() { ++refreshes; }
    void neko_set_spmode_system_proxy(bool enable, bool save = true);
};
#include GR_SYSTEM_PROXY_METHOD

static int failures = 0;
static void check(const char *name, bool ok) {
    failures += !ok; std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    NekoGui::FixtureStore store;
    NekoGui::dataStore = &store;
    MainWindow window;
    window.neko_set_spmode_system_proxy(true);
    check("explicit initial enable applies the local proxy", os.enabled && os.setCalls == 1 && store.spmode_system_proxy);
    check("initial enable saves one remembered selection", store.remember_spmode == QStringList{"system_proxy"} && store.saves == 1);
    // Another client disconnected and disabled the OS proxy, while GreenRhythm
    // still remembers its selected mode. This is the real user failure.
    os.enabled = false; os.httpPort = 0; os.socksPort = 0;
    window.neko_set_spmode_system_proxy(true);
    check("explicit browser Apply repairs externally disabled OS proxy", os.enabled && os.setCalls == 2);
    check("reapplication writes the currently configured endpoint", os.httpPort == 2080 && os.socksPort == 2080);
    check("reapplication retains a single remembered selection", store.remember_spmode == QStringList{"system_proxy"});
    store.inbound_socks_port = 23080;
    window.neko_set_spmode_system_proxy(true, false);
    check("explicit reapply handles an endpoint changed during the session", os.enabled && os.httpPort == 23080 && os.socksPort == 23080 && os.setCalls == 3);
    check("save=false keeps settings writes disabled", store.saves == 2);
    window.neko_set_spmode_system_proxy(false);
    check("an explicit disable clears a selected mode once", !os.enabled && os.clearCalls == 1 && !store.spmode_system_proxy && store.remember_spmode.isEmpty());
    window.neko_set_spmode_system_proxy(false);
    check("repeated disabled selection does not clear OS settings again", os.clearCalls == 1);
    store.remember_enable = false;
    window.neko_set_spmode_system_proxy(true);
    check("unremembered explicit enable still applies OS proxy", os.enabled && os.setCalls == 4 && store.remember_spmode.isEmpty());
    check("each explicit action refreshes the UI", window.refreshes == 6);
    return failures ? 1 : 0;
}
