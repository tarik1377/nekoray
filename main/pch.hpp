// Precompiled header — stable, widely-included headers only, to cut rebuild time.
// Keep this list to things that (a) nearly every translation unit pulls in and
// (b) change ~never. Do NOT add project headers here: they change often and would
// force a full rebuild on every edit, defeating the point.
#pragma once

#ifdef __cplusplus

#include <memory>
#include <functional>
#include <map>
#include <vector>
#include <string>

#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QWidget>
#include <QDebug>
#include <QFile>
#include <QDir>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

#endif
