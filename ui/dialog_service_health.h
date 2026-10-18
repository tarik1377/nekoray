#pragma once
#include "main/ServiceHealth.hpp"
#include <QDialog>
class QComboBox;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QProgressBar;

class DialogServiceHealth final : public QDialog {
    Q_OBJECT
public:
    explicit DialogServiceHealth(QWidget *parent = nullptr, const QList<GreenRhythm::HealthTarget> &targets = {});
    std::function<GreenRhythm::HealthContext()> captureContext;
    std::function<bool()> contextStillCurrent;
    GreenRhythm::ServiceHealth *health;
signals:
    void observeDiscordRequested();
private:
    void begin();
    void refresh();
    QComboBox *service;
    QCheckBox *stability;
    QLabel *verdict;
    QPlainTextEdit *results;
    QPushButton *start, *cancel, *copy, *observe;
    QProgressBar *progress;
    QTimer contextTimer;
    QList<GreenRhythm::HealthTarget> probeTargets;
};
