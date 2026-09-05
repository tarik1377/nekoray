#include "Icon.hpp"

#include "main/NekoGui.hpp"

#include <QPainter>

QPixmap Icon::GetTrayIcon(Icon::TrayIconStatus status) {
    QPixmap pixmap;

    // software embedded icon
    auto pixmap_read = QPixmap(":/neko/" + software_name.toLower() + ".png");
    if (!pixmap_read.isNull()) pixmap = pixmap_read;

    // software pack icon
    pixmap_read = QPixmap("../" + software_name.toLower() + ".png");
    if (!pixmap_read.isNull()) pixmap = pixmap_read;

    // user icon
    pixmap_read = QPixmap("./" + software_name.toLower() + ".png");
    if (!pixmap_read.isNull()) pixmap = pixmap_read;

    if (status == TrayIconStatus::NONE) return pixmap;

    auto p = QPainter(&pixmap);

    auto side = pixmap.width();
    auto radius = side * 0.4;
    auto d = side * 0.3;
    auto margin = side * 0.05;

    // МЕТКА В ТРЕЕ — ТЕМ ЖЕ ЯЗЫКОМ, ЧТО ОКНО. Раньше туннель обозначался
    // КРАСНЫМ: самый частый и самый исправный режим выглядел ошибкой, и
    // человек лез проверять, что стряслось. Зелёный акцент — «наше, включено»,
    // как во всём окне; синий — системный прокси; янтарный — ядро работает,
    // но ни туннель, ни прокси не включены, то есть трафик пока идёт мимо.
    p.setPen(Qt::NoPen);
    if (status == TrayIconStatus::RUNNING) {
        p.setBrush(QBrush(QColor(0xE3, 0xA0, 0x08)));
    } else if (status == TrayIconStatus::SYSTEM_PROXY) {
        p.setBrush(QBrush(QColor(0x4C, 0x9A, 0xFF)));
    } else if (status == TrayIconStatus::VPN) {
        p.setBrush(QBrush(QColor(0x3F, 0xB9, 0x50)));
    }
    p.drawRoundedRect(
        QRect(side - d - margin,
              side - d - margin,
              d,
              d),
        radius,
        radius);
    p.end();

    return pixmap;
}

QPixmap Icon::GetMaterialIcon(const QString &name) {
    QPixmap pixmap(":/icon/material/" + name + ".svg");
    return pixmap;
}
