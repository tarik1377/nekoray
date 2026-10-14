#pragma once

#include "main/BrowserNetworkHints.hpp"
#include <QDialog>
#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;

class DialogWebDiagnostics final : public QDialog {
    Q_OBJECT
public:
    explicit DialogWebDiagnostics(QWidget *parent = nullptr);
    void setProxyEndpoint(quint16 port, const QString &user, const QString &password, bool profileRunning);
    void setHints(const GreenRhythm::BrowserNetworkHints &hints);
signals:
    // MainWindow owns mode selection and persistence. This dialog never edits
    // registry, browser state, routes, profiles or system proxy settings.
    void connectBrowsersRequested();
private:
    void beginCheck();
    GreenRhythm::BrowserNetworkProbe *probe = nullptr;
    GreenRhythm::WebCheckOptions options;
    QLineEdit *address = nullptr;
    QLabel *hintsText = nullptr;
    QLabel *systemResult = nullptr;
    QLabel *greenResult = nullptr;
    QLabel *comparison = nullptr;
    QLabel *addressError = nullptr;
    QPushButton *check = nullptr;
    QPushButton *cancel = nullptr;
    QPushButton *connectBrowsers = nullptr;
    std::optional<GreenRhythm::WebCheckResult> system;
    std::optional<GreenRhythm::WebCheckResult> green;
};
