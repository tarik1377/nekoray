#include <QStyle>
#include <QApplication>
#include <QStyleFactory>
#include <QColor>
#include <QPalette>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QFileSystemWatcher>

#include "ThemeManager.hpp"

ThemeManager *themeManager = new ThemeManager;

extern QString ReadFileText(const QString &path);

// Dev override: if GREENRHYTHM_QSS_DIR is set and holds a same-named css file,
// read the theme from disk instead of the compiled-in :/qrc copy. Returns the
// resolved disk path in *diskPath when the override was used.
static QString ReadThemeCss(const QString &qrcPath, QString *diskPath) {
    const auto devDir = qEnvironmentVariable("GREENRHYTHM_QSS_DIR");
    if (!devDir.isEmpty()) {
        // QDir normalizes mixed separators (env var typically holds backslashes).
        const QString disk = QDir(devDir).absoluteFilePath(QFileInfo(qrcPath).fileName());
        if (QFileInfo::exists(disk)) {
            if (diskPath) *diskPath = disk;
            return ReadFileText(disk);
        }
    }
    return ReadFileText(qrcPath);
}

void ThemeManager::watchDevQss(const QString &diskPath) {
    if (dev_qss_watcher == nullptr) {
        dev_qss_watcher = new QFileSystemWatcher();
        QObject::connect(dev_qss_watcher, &QFileSystemWatcher::fileChanged, dev_qss_watcher, [this](const QString &p) {
            // Editors often replace the file (watch drops) — re-add and re-apply.
            if (!dev_qss_watcher->files().contains(p) && QFile::exists(p)) dev_qss_watcher->addPath(p);
            const auto t = current_theme;
            current_theme = "__reload__"; // bypass the no-op guard in ApplyTheme
            ApplyTheme(t);
        });
    }
    if (!dev_qss_watcher->files().contains(diskPath)) dev_qss_watcher->addPath(diskPath);
}

void ThemeManager::ApplyTheme(const QString &theme) {
    auto internal = [=] {
        if (this->system_style_name.isEmpty()) {
            this->system_style_name = qApp->style()->objectName();
        }
        if (this->current_theme == theme) {
            return;
        }

        bool ok;
        auto themeId = theme.toInt(&ok);

        if (ok) {
            // System & Built-in
            QString qss;

            if (themeId != 0) {
                QString path;
                std::map<QString, QString> replace;
                switch (themeId) {
                    case 1:
                        path = ":/themes/feiyangqingyun/qss/flatgray.css";
                        replace[":/qss/"] = ":/themes/feiyangqingyun/qss/";
                        break;
                    case 2:
                        path = ":/themes/feiyangqingyun/qss/lightblue.css";
                        replace[":/qss/"] = ":/themes/feiyangqingyun/qss/";
                        break;
                    case 3:
                        path = ":/themes/feiyangqingyun/qss/blacksoft.css";
                        replace[":/qss/"] = ":/themes/feiyangqingyun/qss/";
                        break;
                    case 4:
                        // GreenRhythm Modern — flat dark, asset-free (no :/qss/ image refs)
                        path = ":/themes/feiyangqingyun/qss/modern.css";
                        break;
                    default:
                        return;
                }
                QString devDiskPath;
                qss = ReadThemeCss(path, &devDiskPath);
                for (auto const &[a, b]: replace) {
                    qss = qss.replace(a, b);
                }
                if (!devDiskPath.isEmpty()) watchDevQss(devDiskPath);
            }

            auto system_style = QStyleFactory::create(this->system_style_name);

            if (themeId == 0) {
                // system theme
                qApp->setPalette(system_style->standardPalette());
                qApp->setStyle(system_style);
                qApp->setStyleSheet("");
            } else {
                if (themeId == 1 || themeId == 2 || themeId == 3) {
                    // feiyangqingyun theme
                    QString paletteColor = qss.mid(20, 7);
                    qApp->setPalette(QPalette(paletteColor));
                } else if (themeId == 4) {
                    // GreenRhythm Modern — seed a dark base palette so unstyled bits stay dark
                    qApp->setPalette(QPalette(QColor(0x1a, 0x1d, 0x22)));
                } else {
                    // other theme
                    qApp->setPalette(system_style->standardPalette());
                }
                qApp->setStyleSheet(qss);
            }
        } else {
            // QStyleFactory
            const auto &_style = QStyleFactory::create(theme);
            if (_style != nullptr) {
                qApp->setPalette(_style->standardPalette());
                qApp->setStyle(_style);
                qApp->setStyleSheet("");
            }
        }

        current_theme = theme;
    };
    internal();

    auto nekoray_css = ReadFileText(":/neko/neko.css");
    qApp->setStyleSheet(qApp->styleSheet().append("\n").append(nekoray_css));
}
