#pragma once
#include "main/RouteDefaultsPolicy.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace GreenRhythm {
// The same small review is used by the real route dialog and its offscreen test.
class RouteDefaultsReview : public QDialog {
public:
    RouteDefaultsReview(const RouteDefaultsPolicy::Preview &safe,
                        const RouteDefaultsPolicy::Preview &reset, QWidget *parent = nullptr)
        : QDialog(parent), safePlan(safe), resetPlan(reset) {
        setWindowTitle(tr("Стандартные маршруты · версия %1").arg(RouteDefaultsPolicy::Revision));
        setObjectName("routeDefaultsReview");
        auto *layout = new QVBoxLayout(this);
        safeOption = new QRadioButton(tr("Безопасное обновление — сохранить мои правила"), this);
        safeOption->setObjectName("routeDefaultsSafe");
        resetOption = new QRadioButton(tr("Полный сброс маршрутов этой схемы"), this);
        resetOption->setObjectName("routeDefaultsReset");
        safeOption->setChecked(true);
        layout->addWidget(safeOption);
        layout->addWidget(resetOption);
        summary = new QLabel(this);
        summary->setObjectName("routeDefaultsPreview");
        summary->setWordWrap(true);
        layout->addWidget(summary);
        acknowledgement = new QCheckBox(tr("Заменить мои маршруты и правила программ стандартными"), this);
        acknowledgement->setObjectName("routeDefaultsResetConfirm");
        layout->addWidget(acknowledgement);
        auto *note = new QLabel(tr("Перед применением сохраняется резервная копия. Локальный DNS и глобальные правила сохраняются. Для текущего подключения может потребоваться перезапуск."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
        applyButton = buttons->addButton(tr("Применить"), QDialogButtonBox::AcceptRole);
        applyButton->setObjectName("routeDefaultsApplyConfirmed");
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
        connect(safeOption, &QRadioButton::toggled, this, [this] { refresh(); });
        connect(acknowledgement, &QCheckBox::toggled, this, [this] { refresh(); });
        refresh();
        resize(530, sizeHint().height());
    }
    RouteDefaultsPolicy::Mode mode() const {
        return safeOption->isChecked() ? RouteDefaultsPolicy::Mode::SafeUpgrade : RouteDefaultsPolicy::Mode::ResetRoutes;
    }
private:
    RouteDefaultsPolicy::Preview safePlan, resetPlan;
    QRadioButton *safeOption, *resetOption;
    QCheckBox *acknowledgement;
    QLabel *summary;
    QPushButton *applyButton;
    void refresh() {
        const bool safe = safeOption->isChecked();
        const auto &plan = safe ? safePlan : resetPlan;
        summary->setText(tr("%1 полей изменится. Правил добавится: %2, удалится: %3.\n%4")
            .arg(plan.changedFields.size()).arg(plan.addedRules).arg(plan.removedRules)
            .arg(safe ? tr("Свои DNS и правила программ сохраняются; исправляются только узнаваемые старые значения и добавляются недостающие защиты.")
                      : tr("Свои списки маршрутов, DNS интернета и правила программ будут заменены стандартными.")));
        acknowledgement->setVisible(!safe);
        applyButton->setEnabled(safe || acknowledgement->isChecked());
    }
};
} // namespace GreenRhythm
