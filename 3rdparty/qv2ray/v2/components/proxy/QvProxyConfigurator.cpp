#include "QvProxyConfigurator.hpp"

#ifdef Q_OS_WIN
#include "sys/windows/WindowsProxy.hpp"
#endif

#include <QStandardPaths>
#include <QProcess>

#include "3rdparty/fix_old_qt.h"
#include "3rdparty/qv2ray/wrapper.hpp"
#include "fmt/Preset.hpp"
#include "main/NekoGui.hpp"

#define QV_MODULE_NAME "SystemProxy"

#define QSTRN(num) QString::number(num)

namespace Qv2ray::components::proxy {

    using ProcessArgument = QPair<QString, QStringList>;
#ifdef Q_OS_MACOS
    QStringList macOSgetNetworkServices() {
        QProcess p;
        p.setProgram("/usr/sbin/networksetup");
        p.setArguments(QStringList{"-listallnetworkservices"});
        p.start();
        p.waitForStarted();
        p.waitForFinished();
        LOG(p.errorString());
        auto str = p.readAllStandardOutput();
        auto lines = SplitLines(str);
        QStringList result;

        // Start from 1 since first line is unneeded.
        for (auto i = 1; i < lines.count(); i++) {
            // * means disabled.
            if (!lines[i].contains("*")) {
                result << lines[i];
            }
        }

        LOG("Found " + QSTRN(result.size()) + " network services: " + result.join(";"));
        return result;
    }
#endif
    bool SetSystemProxy(int httpPort, int socksPort, QString *error) {
        const QString &address = "127.0.0.1";
        bool hasHTTP = (httpPort > 0 && httpPort < 65536);
        bool hasSOCKS = (socksPort > 0 && socksPort < 65536);

#ifdef Q_OS_WIN
        if (!hasHTTP) {
            LOG("Nothing?");
            return false;
        } else {
            LOG("Qv2ray will set system proxy to use HTTP");
        }
#else
        if (!hasHTTP && !hasSOCKS) {
            LOG("Nothing?");
            return false;
        }

        if (hasHTTP) {
            LOG("Qv2ray will set system proxy to use HTTP");
        }

        if (hasSOCKS) {
            LOG("Qv2ray will set system proxy to use SOCKS");
        }
#endif

#ifdef Q_OS_WIN
        QString str = NekoGui::dataStore->system_proxy_format;
        if (str.isEmpty()) str = Preset::Windows::system_proxy_format[0];
        str = str.replace("{ip}", address)
                  .replace("{http_port}", Int2String(httpPort))
                  .replace("{socks_port}", Int2String(socksPort));
        QString reason;
        const bool applied = GreenRhythm::WindowsProxy::enable(str, reason);
        if (error) *error = reason;
        return applied;
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy", "mode", "manual"}};
        //
        bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" ||
                     qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
        const auto configPath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        //
        // Configure HTTP Proxies for HTTP, FTP and HTTPS
        if (hasHTTP) {
            // iterate over protocols...
            for (const auto &protocol: QStringList{"http", "ftp", "https"}) {
                // for GNOME:
                {
                    actions << ProcessArgument{"gsettings",
                                               {"set", "org.gnome.system.proxy." + protocol, "host", address}};
                    actions << ProcessArgument{"gsettings",
                                               {"set", "org.gnome.system.proxy." + protocol, "port", QSTRN(httpPort)}};
                }

                // for KDE:
                if (isKDE) {
                    actions << ProcessArgument{"kwriteconfig5",
                                               {"--file", configPath + "/kioslaverc", //
                                                "--group", "Proxy Settings",          //
                                                "--key", protocol + "Proxy",          //
                                                "http://" + address + " " + QSTRN(httpPort)}};
                }
            }
        }

        // Configure SOCKS5 Proxies
        if (hasSOCKS) {
            // for GNOME:
            {
                actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy.socks", "host", address}};
                actions << ProcessArgument{"gsettings",
                                           {"set", "org.gnome.system.proxy.socks", "port", QSTRN(socksPort)}};

                // for KDE:
                if (isKDE) {
                    actions << ProcessArgument{"kwriteconfig5",
                                               {"--file", configPath + "/kioslaverc", //
                                                "--group", "Proxy Settings",          //
                                                "--key", "socksProxy",                //
                                                "socks://" + address + " " + QSTRN(socksPort)}};
                }
            }
        }
        // Setting Proxy Mode to Manual
        {
            // for GNOME:
            {
                actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy", "mode", "manual"}};
            }

            // for KDE:
            if (isKDE) {
                actions << ProcessArgument{"kwriteconfig5",
                                           {"--file", configPath + "/kioslaverc", //
                                            "--group", "Proxy Settings",          //
                                            "--key", "ProxyType", "1"}};
            }
        }

        // Notify kioslaves to reload system proxy configuration.
        if (isKDE) {
            actions << ProcessArgument{"dbus-send",
                                       {"--type=signal", "/KIO/Scheduler",                 //
                                        "org.kde.KIO.Scheduler.reparseSlaveConfiguration", //
                                        "string:''"}};
        }
        // Execute them all!
        //
        // note: do not use std::all_of / any_of / none_of,
        // because those are short-circuit and cannot guarantee atomicity.
        QList<bool> results;
        for (const auto &action: actions) {
            // execute and get the code
            const auto returnCode = QProcess::execute(action.first, action.second);
            // print out the commands and result codes
            DEBUG(QStringLiteral("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
            // give the code back
            results << (returnCode == QProcess::NormalExit);
        }

        if (results.count(true) != actions.size()) {
            LOG("Something wrong when setting proxies.");
        }
#else

        for (const auto &service: macOSgetNetworkServices()) {
            LOG("Setting proxy for interface: " + service);
            if (hasHTTP) {
                QProcess::execute("/usr/sbin/networksetup", {"-setwebproxystate", service, "on"});
                QProcess::execute("/usr/sbin/networksetup", {"-setsecurewebproxystate", service, "on"});
                QProcess::execute("/usr/sbin/networksetup", {"-setwebproxy", service, address, QSTRN(httpPort)});
                QProcess::execute("/usr/sbin/networksetup", {"-setsecurewebproxy", service, address, QSTRN(httpPort)});
            }

            if (hasSOCKS) {
                QProcess::execute("/usr/sbin/networksetup", {"-setsocksfirewallproxystate", service, "on"});
                QProcess::execute("/usr/sbin/networksetup", {"-setsocksfirewallproxy", service, address, QSTRN(socksPort)});
            }
        }

#endif
        return true;
    }

    bool ClearSystemProxy(QString *error) {
        LOG("Clearing System Proxy");

#ifdef Q_OS_WIN
        QString reason;
        const bool cleared = GreenRhythm::WindowsProxy::disable(reason);
        if (error) *error = reason;
        return cleared;
#elif defined(Q_OS_LINUX)
        QList<ProcessArgument> actions;
        const bool isKDE = qEnvironmentVariable("XDG_SESSION_DESKTOP") == "KDE" ||
                           qEnvironmentVariable("XDG_SESSION_DESKTOP") == "plasma";
        const auto configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);

        // Setting System Proxy Mode to: None
        {
            // for GNOME:
            {
                actions << ProcessArgument{"gsettings", {"set", "org.gnome.system.proxy", "mode", "none"}};
            }

            // for KDE:
            if (isKDE) {
                actions << ProcessArgument{"kwriteconfig5",
                                           {"--file", configRoot + "/kioslaverc", //
                                            "--group", "Proxy Settings",          //
                                            "--key", "ProxyType", "0"}};
            }
        }

        // Notify kioslaves to reload system proxy configuration.
        if (isKDE) {
            actions << ProcessArgument{"dbus-send",
                                       {"--type=signal", "/KIO/Scheduler",                 //
                                        "org.kde.KIO.Scheduler.reparseSlaveConfiguration", //
                                        "string:''"}};
        }

        // Execute the Actions
        for (const auto &action: actions) {
            // execute and get the code
            const auto returnCode = QProcess::execute(action.first, action.second);
            // print out the commands and result codes
            DEBUG(QStringLiteral("[%1] Program: %2, Args: %3").arg(returnCode).arg(action.first).arg(action.second.join(";")));
        }

#else
        for (const auto &service: macOSgetNetworkServices()) {
            LOG("Clearing proxy for interface: " + service);
            QProcess::execute("/usr/sbin/networksetup", {"-setautoproxystate", service, "off"});
            QProcess::execute("/usr/sbin/networksetup", {"-setwebproxystate", service, "off"});
            QProcess::execute("/usr/sbin/networksetup", {"-setsecurewebproxystate", service, "off"});
            QProcess::execute("/usr/sbin/networksetup", {"-setsocksfirewallproxystate", service, "off"});
        }

#endif
        return true;
    }
} // namespace Qv2ray::components::proxy
