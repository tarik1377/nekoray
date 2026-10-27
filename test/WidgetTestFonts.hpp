#pragma once
#include <QFile>
#include <QFontDatabase>

// The offscreen Windows plugin does not populate the native font database.
// Register installed fonts before applying the real application's font policy.
inline void registerWidgetTestFonts() {
#ifdef Q_OS_WIN
    for (const auto *path : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/segoeuib.ttf", "C:/Windows/Fonts/SegUIVar.ttf"})
        if (QFile::exists(QString::fromLatin1(path))) QFontDatabase::addApplicationFont(QString::fromLatin1(path));
#endif
}
