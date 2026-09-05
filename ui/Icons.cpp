#include "ui/Icons.hpp"

#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QSvgRenderer>

namespace GreenRhythm::Icons {

    namespace {
        qreal screenScale() {
            const auto *screen = QGuiApplication::primaryScreen();
            return screen != nullptr ? screen->devicePixelRatio() : 1.0;
        }

        QByteArray coloured(const QString &name, const QColor &color) {
            QFile f(QStringLiteral(":/icon/%1.svg").arg(name));
            if (!f.open(QIODevice::ReadOnly)) return {};
            QByteArray svg = f.readAll();
            // Подстановка в текст, а не через QPalette: так работает в любом
            // месте — в кнопке, в подписи, в ячейке таблицы.
            svg.replace("currentColor", color.name(QColor::HexRgb).toLatin1());
            return svg;
        }
    } // namespace

    QPixmap pixmap(const QString &name, const QColor &color, int size) {
        const qreal dpr = screenScale();
        QPixmap pm(qRound(size * dpr), qRound(size * dpr));
        pm.fill(Qt::transparent);
        const auto svg = coloured(name, color);
        if (!svg.isEmpty()) {
            QSvgRenderer r(svg);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            r.render(&p);
        }
        pm.setDevicePixelRatio(dpr);
        return pm;
    }

    QIcon icon(const QString &name, const QColor &normal, const QColor &active, int size) {
        QIcon ic;
        const auto n = pixmap(name, normal, size);
        const auto a = pixmap(name, active, size);
        ic.addPixmap(n, QIcon::Normal, QIcon::Off);
        ic.addPixmap(a, QIcon::Active, QIcon::Off);
        ic.addPixmap(a, QIcon::Normal, QIcon::On);
        ic.addPixmap(a, QIcon::Active, QIcon::On);
        ic.addPixmap(a, QIcon::Selected, QIcon::Off);
        ic.addPixmap(a, QIcon::Selected, QIcon::On);
        return ic;
    }

} // namespace GreenRhythm::Icons
