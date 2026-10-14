#include "test/WindowsProxyFixture.hpp"
#include <QCoreApplication>
#include <QObject>
#include <QStringList>
#include <cstdio>
using namespace ProxyTest;
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
static NativeFixture os;
static WinInetApi api(os.functions());
static Controller controller(api);
static int warnings = 0;
static QStringList logs;
static const QString software_name = "GreenRhythm test";
static void MessageBoxWarning(const QString &, const QString &) { ++warnings; }
static void MW_show_log(const QString &message) { logs.append(message); }
// The actual UI controller and real WinINET adapter are compiled here. Only
// settings storage, modal reporting, and native Windows function calls are fake.
bool SetSystemProxy(int http, int, QString *error) {
    QString reason;
    const bool ok = controller.enable(QString("127.0.0.1:%1").arg(http), reason);
    if (error) *error = reason;
    return ok;
}
bool ClearSystemProxy(QString *error) {
    QString reason;
    const bool ok = controller.disable(reason);
    if (error) *error = reason;
    return ok;
}
class MainWindow : public QObject {
public:
    int refreshes = 0;
    void refresh_status() { ++refreshes; }
    bool neko_set_spmode_system_proxy(bool enable, bool save = true);
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
    check("explicit initial enable applies and confirms LAN proxy", window.neko_set_spmode_system_proxy(true)
          && os.state.flags == 3 && os.writes == 1 && store.spmode_system_proxy);
    check("initial enable saves one remembered selection", store.remember_spmode == QStringList{"system_proxy"} && store.saves == 1);
    // Another client disabled WinINET, while the GUI still remembers its mode.
    os.state.flags = Direct;
    check("explicit browser Apply repairs externally disabled OS proxy", window.neko_set_spmode_system_proxy(true) && os.state.flags == 3 && os.writes == 2);
    check("reapplication writes the currently configured endpoint", os.state.server == "127.0.0.1:2080");
    check("reapplication retains a single remembered selection", store.remember_spmode == QStringList{"system_proxy"});
    store.inbound_socks_port = 23080;
    check("explicit reapply handles endpoint changes", window.neko_set_spmode_system_proxy(true, false)
          && os.state.server == "127.0.0.1:23080" && os.writes == 3);
    check("save=false keeps settings writes disabled", store.saves == 2);
    check("disable restores prior LAN state", window.neko_set_spmode_system_proxy(false) && os.state.flags == Direct
          && os.writes == 4 && !store.spmode_system_proxy && store.remember_spmode.isEmpty());
    window.neko_set_spmode_system_proxy(false);
    check("repeated disable cannot write settings it does not own", os.writes == 4);
    store.remember_enable = false;
    check("unremembered enable applies without remembered selection", window.neko_set_spmode_system_proxy(true)
          && os.state.flags == 3 && os.writes == 5 && store.remember_spmode.isEmpty());
    os.state = {Direct | Manual, "foreign-proxy.example:9000"};
    const auto writes = os.writes;
    check("actual UI disable preserves another client's endpoint", window.neko_set_spmode_system_proxy(false)
          && os.writes == writes && os.state.server == "foreign-proxy.example:9000" && !store.spmode_system_proxy);
    const auto saves = store.saves;
    os.failWrites = 1;
    check("failed native enable returns failure instead of selected state", !window.neko_set_spmode_system_proxy(true)
          && !store.spmode_system_proxy && store.saves == saves && os.state.server == "foreign-proxy.example:9000");
    check("failed enable is logged and visible", warnings == 1 && logs.size() == 1 && logs.last().contains("5"));
    check("explicit retry works after a failed enable", window.neko_set_spmode_system_proxy(true) && store.spmode_system_proxy);
    const auto selectedSaves = store.saves;
    os.failWrites = 1;
    check("failed cleanup preserves selection and settings for retry", !window.neko_set_spmode_system_proxy(false)
          && store.spmode_system_proxy && store.saves == selectedSaves && os.state.server == "127.0.0.1:23080");
    check("cleanup failure is reported", warnings == 2 && logs.size() == 2);
    check("cleanup retry restores the foreign prior", window.neko_set_spmode_system_proxy(false)
          && !store.spmode_system_proxy && os.state.server == "foreign-proxy.example:9000");
    check("all native calls stay LAN-only and query allocations are freed", os.validCalls && os.freed == os.queries);
    check("success and failure paths refresh the UI", window.refreshes == 11);
    return failures ? 1 : 0;
}
