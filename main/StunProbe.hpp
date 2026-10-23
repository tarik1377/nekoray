#pragma once
#include <QByteArray>
#include <QtEndian>

namespace GreenRhythm::StunProbe {
// RFC 8489 Binding: a fresh 96-bit transaction, cookie, exact lengths and a
// valid mapped-address attribute. Mapped addresses never enter the report.
inline QByteArray request(const QByteArray &transaction) {
    return transaction.size() == 12 ? QByteArray::fromHex("000100002112a442") + transaction : QByteArray();
}
inline bool validReply(const QByteArray &message, const QByteArray &transaction) {
    if (transaction.size() != 12 || message.size() < 20 || message.left(2) != QByteArray::fromHex("0101")
        || message.mid(4, 4) != QByteArray::fromHex("2112a442") || message.mid(8, 12) != transaction) return false;
    const auto length = qFromBigEndian<quint16>(message.constData() + 2);
    if (length % 4 || message.size() != 20 + length) return false;
    bool mapped = false;
    for (int offset = 20; offset < message.size();) {
        if (offset + 4 > message.size()) return false;
        const auto type = qFromBigEndian<quint16>(message.constData() + offset);
        const auto size = qFromBigEndian<quint16>(message.constData() + offset + 2);
        const int end = offset + 4 + ((size + 3) & ~3);
        if (end > message.size()) return false;
        if (type == 0x0020 || type == 0x0001) {
            if (size != 8 && size != 20) return false;
            const auto family = quint8(message[offset + 5]);
            if (message[offset + 4] != 0 || (size == 8 ? family != 1 : family != 2)) return false;
            const auto port = qFromBigEndian<quint16>(message.constData() + offset + 6);
            if ((type == 0x0020 ? port ^ 0x2112 : port) == 0) return false;
            mapped = true;
        }
        offset = end;
    }
    return mapped;
}
} // namespace GreenRhythm::StunProbe
