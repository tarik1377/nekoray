#pragma once
#include "main/ProgramRouting.hpp"
#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
namespace GreenRhythm {
class RobloxRoutingDialog : public QDialog {
public:
    using Commit = std::function<bool(const ProgramRouting::State &, QString *)>;
    explicit RobloxRoutingDialog(const ProgramRouting::State &saved, bool tunnel, QWidget *parent = nullptr);
    void setCommitter(Commit callback) { commit = std::move(callback); }
    void setInspector(std::function<void(const QString &)> callback) { inspector = std::move(callback); }
    void accept() override;
private:
    ProgramRouting::State original;
    QComboBox *player = nullptr, *studio = nullptr;
    QLabel *status = nullptr;
    QPushButton *save = nullptr;
    Commit commit;
    std::function<void(const QString &)> inspector;
};
}
