#ifndef DIALOG_BASIC_SETTINGS_H
#define DIALOG_BASIC_SETTINGS_H

#include <QDialog>
#include <QJsonObject>

namespace Ui {
    class DialogBasicSettings;
}
class QCheckBox;
class QLabel;
class QPushButton;

class DialogBasicSettings : public QDialog {
    Q_OBJECT

public:
    explicit DialogBasicSettings(QWidget *parent = nullptr);

    ~DialogBasicSettings();

public slots:

    void accept();

private:
    Ui::DialogBasicSettings *ui;
    QCheckBox *geoAutoUpdate = nullptr;
    QLabel *geoStatus = nullptr;
    QPushButton *geoRefresh = nullptr;
    void refreshGeoDataStatus();

    struct {
        QJsonObject extraCore;
        QString custom_inbound;
        bool needRestart = false;
    } CACHE;

private slots:

    void refresh_auth();

    void on_set_custom_icon_clicked();

    void on_inbound_auth_clicked();

    void on_core_settings_clicked();
};

#endif // DIALOG_BASIC_SETTINGS_H
