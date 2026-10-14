#include "sys/windows/WindowsProxyApi.hpp"
#include "sys/windows/WindowsProxy.hpp"

namespace GreenRhythm::WindowsProxy {
namespace {
WinInetApi api;
Controller controller(api);
}
bool enable(const QString &server, QString &error) { return controller.enable(server, error); }
bool disable(QString &error) { return controller.disable(error); }
}
