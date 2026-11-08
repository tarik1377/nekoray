#pragma once
#include <QDialog>
#include <functional>
class QCheckBox;
class QLabel;
class QPlainTextEdit;
namespace GreenRhythm {
class SiteRoutingDialog final : public QDialog {
public:
    explicit SiteRoutingDialog(const QString &saved, QWidget *parent = nullptr);
    void setCommitter(std::function<bool(const QString &, QString *)> callback) { commit = std::move(callback); }
    void accept() override;
private:
    QCheckBox *enabled;
    QPlainTextEdit *automatic, *direct, *proxy;
    QLabel *status;
    bool valid = true;
    std::function<bool(const QString &, QString *)> commit;
};
}
