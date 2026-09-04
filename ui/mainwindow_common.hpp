#pragma once

/**
 * Общий пролог включений для частей главного окна.
 *
 * ЗАЧЕМ. mainwindow.cpp разросся до 4600 строк — при принятом в этом проекте
 * пределе в 800 — и был разрезан на части: диагностика, починка сети,
 * автопилот, онбординг. Каждой части нужен примерно один и тот же набор
 * включений, и повторить его четырежды значит однажды получить четыре
 * разошедшихся набора.
 *
 * Здесь ТОЛЬКО включения. Ни объявлений, ни определений: части остаются
 * методами MainWindow, а их объявления живут в mainwindow.h, как и раньше.
 * Переезжают тела, а не устройство класса.
 */

#include "./ui_mainwindow.h"
#include "mainwindow.h"

#include "fmt/Preset.hpp"
#include "db/ProfileFilter.hpp"
#include "db/ConfigBuilder.hpp"
#include "sub/GroupUpdater.hpp"
#include "sys/ExternalProcess.hpp"
#include "sys/AutoRun.hpp"
#include "main/BrandingConstants.hpp"

#include "ui/ThemeManager.hpp"
#include "ui/Icon.hpp"
#include "ui/edit/dialog_edit_profile.h"
#include "ui/dialog_basic_settings.h"
#include "ui/dialog_manage_groups.h"
#include "ui/dialog_greenrhythm.h"
#include "ui/dialog_whatbroke.h"
#include "main/PortHealth.hpp"
#include "db/traffic/TrafficLooper.hpp"
#include "ui/MainShell.hpp"
#include "ui/ServerCardDelegate.hpp"

#include <QProcess>
#include <QTemporaryDir>
#include "ui/dialog_manage_routes.h"
#include "ui/dialog_vpn_settings.h"
#include "ui/dialog_hotkey.h"
#include "ui/dialog_relay_activate.h"
#include "main/DeviceCredentials.hpp"

#include "3rdparty/fix_old_qt.h"
#include "3rdparty/qrcodegen.hpp"
#include "3rdparty/VT100Parser.hpp"
#include "3rdparty/qv2ray/v2/components/proxy/QvProxyConfigurator.hpp"
#include "sys/ForeignTunnels.hpp"
#include "sys/WinShell.hpp"
#include "fmt/RelayBean.hpp"
#include "ui/dialog_macos_mode.h"

#ifdef Q_OS_MACOS
#include "sys/macos/MacProxyController.hpp"
#include "sys/macos/PacBuilder.hpp"
#endif

#ifndef NKR_NO_ZXING
#include "3rdparty/ZxingQtReader.hpp"
#endif

#ifdef Q_OS_WIN
#include "3rdparty/WinCommander.hpp"
#else
#ifdef Q_OS_LINUX
#include "sys/linux/LinuxCap.h"
#endif
#include <unistd.h>
#endif

#include <QClipboard>
#include <QLabel>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QColor>
#include <QBrush>
#include <QScrollBar>
#include <QScreen>
#include <QDesktopServices>
#include <QInputDialog>
#include <QThread>
#include <QTimer>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFont>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QRegularExpression>
#include <QDateTime>
#include <QTcpSocket>
#include <QSslSocket>
#include <QHostInfo>
#include <QProgressDialog>
#include <QSysInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QFileDialog>
#include <QMenu>
#include <QHostAddress>

/**
 * Добавить строку в текущий набор правил маршрутизации и сразу сохранить.
 *
 * Жил в mainwindow.cpp, а нужен и в вынесенной части — контекстное меню
 * соединения предлагает «отправить этот адрес напрямую». Пусть лежит рядом с
 * общим прологом, а не дублируется.
 */
#define ADD_TO_CURRENT_ROUTE(a, b)                                                                   \
    NekoGui::dataStore->routing->a = (SplitLines(NekoGui::dataStore->routing->a) << (b)).join("\n"); \
    NekoGui::dataStore->routing->Save();
