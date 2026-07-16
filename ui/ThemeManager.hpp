#pragma once

#include <QString>

class QFileSystemWatcher;

class ThemeManager {
public:
    QString system_style_name = "";
    QString current_theme = "0"; // int: 0:system 1+:builtin string: QStyleFactory

    void ApplyTheme(const QString &theme);

private:
    // Dev-only live QSS reload: when GREENRHYTHM_QSS_DIR points at the theme source
    // dir, css is read from disk and re-applied on save — style tweaks need no rebuild.
    QFileSystemWatcher *dev_qss_watcher = nullptr;
    void watchDevQss(const QString &diskPath);
};

extern ThemeManager *themeManager;
