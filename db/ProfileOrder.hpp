#pragma once

#include <QList>
#include <QSet>

namespace NekoGui {
inline QList<int> CompleteProfileOrder(const QList<int> &preferred, const QList<int> &available) {
    QSet<int> remaining(available.begin(), available.end());
    QList<int> result;
    result.reserve(available.size());
    for (const int id : preferred)
        if (remaining.remove(id)) result.append(id);
    for (const int id : available)
        if (remaining.remove(id)) result.append(id);
    return result;
}
} // namespace NekoGui
