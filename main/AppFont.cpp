#include "main/AppFont.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>

namespace GreenRhythm {

    void applyAppFont(QApplication &app) {
#ifdef Q_OS_WIN
        const QStringList families = QFontDatabase::families();
        // Variable Text — начертание для основного текста; «Segoe UI Variable»
        // без уточнения на части машин отдаёт Display, слишком лёгкий в мелком
        // кегле.
        const QString family = families.contains(QStringLiteral("Segoe UI Variable Text"))
                                   ? QStringLiteral("Segoe UI Variable Text")
                                   : QStringLiteral("Segoe UI");
        QFont f(family, 10);
        f.setStyleStrategy(QFont::PreferAntialias);
        app.setFont(f);
#else
        Q_UNUSED(app)
#endif
    }

} // namespace GreenRhythm
