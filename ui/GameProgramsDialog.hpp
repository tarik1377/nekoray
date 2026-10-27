#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QStringList>
#include <functional>

class QLabel;
class QPushButton;
class QTableWidget;
class QScrollArea;

namespace GreenRhythm {
class GameProgramsDialog : public QDialog {
public:
    using Commit = std::function<bool(const QString &, QString *)>;
    explicit GameProgramsDialog(const QString &saved, bool tunnelEnabled, QWidget *parent = nullptr);
    void setCommitter(Commit callback) { commit = std::move(callback); }
    // Injection keeps interaction tests independent of OS process enumeration.
    void setRunningProvider(std::function<QStringList()> callback) { runningProvider = std::move(callback); }
    void setInspector(std::function<void(const QString &)> callback);
    bool addExecutable(const QString &path);
    QString serializedRules() const;
    bool changed() const { return repaired || original != draft; }
    void accept() override;
private:
    void addRow(const QString &title, const QStringList &programs);
    void chooseRunning();
    void updateSave();
    QTableWidget *table = nullptr;
    QScrollArea *scroll = nullptr;
    QLabel *status = nullptr;
    QPushButton *save = nullptr;
    QPushButton *inspect = nullptr;
    QJsonObject original, draft;
    QString raw;
    bool valid = false;
    bool repaired = false;
    Commit commit;
    std::function<QStringList()> runningProvider;
    std::function<void(const QString &)> inspector;
};
} // namespace GreenRhythm
