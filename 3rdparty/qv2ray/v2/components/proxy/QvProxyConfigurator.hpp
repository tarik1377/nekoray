#pragma once
#include <QHostAddress>
#include <QObject>
#include <QString>
//
namespace Qv2ray::components::proxy {
    bool ClearSystemProxy(QString *error = nullptr);
    bool SetSystemProxy(int http_port, int socks_port, QString *error = nullptr);
} // namespace Qv2ray::components::proxy

using namespace Qv2ray::components;
using namespace Qv2ray::components::proxy;
