#pragma once
#include <QDialog>
#include <functional>
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
namespace GreenRhythm {
class TunBypassDialog : public QDialog {
public:
    explicit TunBypassDialog(const QString &saved, QWidget *parent = nullptr);
    void setCommitter(std::function<bool(const QString &, QString *)> value) { commit = std::move(value); }
    void accept() override;
private:
    QString original;
    QPlainTextEdit *editor;
    QLabel *status;
    QPushButton *save;
    QScrollArea *scroll;
    std::function<bool(const QString &, QString *)> commit;
};
}
