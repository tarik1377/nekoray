#pragma once
#include <QString>

namespace GreenRhythm::WindowsProxy {
bool enable(const QString &server, QString &error);
bool disable(QString &error);
}
