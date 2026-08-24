#pragma once

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
    class DialogRelayActivate;
}
QT_END_NAMESPACE

/**
 * Активация резервного подключения на этом устройстве.
 *
 * ЧЕГО ЗДЕСЬ НЕТ И НЕ ДОЛЖНО ПОЯВИТЬСЯ: ни одного из выданных полей — ни
 * адреса, ни ключей, ни тега устройства. Человеку они не нужны ни для чего, а
 * показанное однажды попадает на снимок экрана, который он пришлёт в поддержку.
 * Диалог показывает состояние, почту аккаунта и дату проверки.
 *
 * Пароля здесь тоже нет. Часть клиентов зарегистрирована через Telegram и
 * пароля не имеет вовсе — для них код и придуман; а поле, которого нет,
 * невозможно ни подсмотреть, ни залогировать.
 */
class DialogRelayActivate : public QDialog {
    Q_OBJECT

public:
    explicit DialogRelayActivate(QWidget *parent = nullptr);

    /** Появился ли новый профиль — вызывающему надо перерисовать список. */
    [[nodiscard]] bool ProfileAdded() const { return profileAdded; }

    ~DialogRelayActivate() override;

private:
    Ui::DialogRelayActivate *ui;

    /** Идёт ли сейчас запрос — чтобы второе нажатие не завело второй. */
    bool busy = false;

    /**
     * Завели ли профиль за время этого диалога.
     *
     * Нужно вызывающему: список профилей сам себя не перерисовывает, и без
     * этого новая строка появилась бы только после перезапуска.
     */
    bool profileAdded = false;

    /** Куда ведёт кнопка действия, когда она видна. */
    QString actionUrl;

    void dressUp();
    void repaintState();
    void setBusy(bool on);
    void showResult(const QString &text, bool bad);
    void showAction(const QString &text, const QString &url);
};
