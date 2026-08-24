#pragma once

#include <QWidget>
#include "profile_editor.h"

QT_BEGIN_NAMESPACE
namespace Ui {
    class EditRelay;
}
QT_END_NAMESPACE

/**
 * Редактор профиля резервного подключения.
 *
 * Полей тут три, и это не бедность, а всё, что человеку можно и нужно менять.
 * Реквизиты доступа он не вводит и не видит: их выдаёт сайт по коду, и лежат
 * они в запечатанном device.dat, а не в профиле.
 */
class EditRelay : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditRelay(QWidget *parent = nullptr);

    ~EditRelay() override;

    void onStart(std::shared_ptr<NekoGui::ProxyEntity> _ent) override;

    bool onEnd() override;

private:
    Ui::EditRelay *ui;
    std::shared_ptr<NekoGui::ProxyEntity> ent;

    void repaintState();
};
