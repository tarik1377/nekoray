#pragma once

#include "main/BrowserNetworkHints.hpp"
#include "main/ProgramRouting.hpp"
#include <QDialog>
#include <functional>
#include <optional>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;

class DialogWebDiagnostics final : public QDialog {
    Q_OBJECT
public:
    explicit DialogWebDiagnostics(QWidget *parent = nullptr);
    void setProxyEndpoint(quint16 port, const QString &user, const QString &password, bool profileRunning);
    void setHints(const GreenRhythm::BrowserNetworkHints &hints);
    void setProgramRouting(const GreenRhythm::ProgramRouting::State &state, bool internalTunActive);
    void setRoutingCommitter(std::function<bool(const QString &, const GreenRhythm::ProgramRouting::State &, QString *)> callback);
signals:
    // MainWindow owns mode selection and persistence. This dialog never edits
    // registry, browser state, routes, profiles or system proxy settings.
    void connectBrowsersRequested();
    void siteRoutingRequested();
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
    QLabel *routingHint = nullptr;
    QLabel *routingStatus = nullptr;
    QComboBox *routingBrowser = nullptr;
    QPushButton *siteRules = nullptr;
    GreenRhythm::ProgramRouting::State routingState;
    bool tunActive = false;
    std::function<bool(const QString &, const GreenRhythm::ProgramRouting::State &, QString *)> routingCommit;
    std::optional<GreenRhythm::WebCheckResult> system;
    std::optional<GreenRhythm::WebCheckResult> green;
};
