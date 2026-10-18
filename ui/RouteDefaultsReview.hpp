#pragma once
#include "main/RouteDefaultsPolicy.hpp"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
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
        auto *intro = new QLabel(tr("Проверьте изменения перед применением. Текущая схема пока не меняется."), this);
        intro->setWordWrap(true);
        layout->addWidget(intro);
        safeOption = new QRadioButton(tr("Обновить без сброса моих правил (рекомендуется)"), this);
        safeOption->setObjectName("routeDefaultsSafe");
        resetOption = new QRadioButton(tr("Полный сброс — заменить мои правила стандартными"), this);
        resetOption->setObjectName("routeDefaultsReset");
        safeOption->setChecked(true);
        layout->addWidget(safeOption);
        layout->addWidget(resetOption);
        summary = new QLabel(this);
        summary->setObjectName("routeDefaultsPreview");
        summary->setWordWrap(true);
        layout->addWidget(summary);
        changesScroll = new QScrollArea(this);
        changesScroll->setObjectName("routeDefaultsChangesScroll");
        changesScroll->setWidgetResizable(true);
        changesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        changesScroll->setMinimumHeight(100);
        changesScroll->setMaximumHeight(220);
        changes = new QLabel(changesScroll);
        changes->setObjectName("routeDefaultsChangedFields");
        changes->setTextFormat(Qt::PlainText);
        changes->setWordWrap(true);
        changes->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        changes->setMargin(10);
        changesScroll->setWidget(changes);
        layout->addWidget(changesScroll, 1);
        noChanges = new QLabel(this);
        noChanges->setObjectName("routeDefaultsNoChanges");
        noChanges->setWordWrap(true);
        layout->addWidget(noChanges);
        acknowledgement = new QCheckBox(tr("Я согласен заменить мои маршруты и правила программ этой схемы"), this);
        acknowledgement->setObjectName("routeDefaultsResetConfirm");
        layout->addWidget(acknowledgement);
        note = new QLabel(tr("Перед применением сохраняется резервная копия. Локальный DNS и глобальные правила сохраняются. Для текущего подключения может потребоваться перезапуск."), this);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *buttons = new QDialogButtonBox(this);
        applyButton = buttons->addButton(tr("Применить изменения"), QDialogButtonBox::AcceptRole);
        applyButton->setObjectName("routeDefaultsApplyConfirmed");
        applyButton->setAutoDefault(false);
        leaveButton = buttons->addButton(tr("Оставить как есть"), QDialogButtonBox::RejectRole);
        leaveButton->setObjectName("routeDefaultsLeaveUnchanged");
        leaveButton->setAutoDefault(false);
        leaveButton->setDefault(true);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(applyButton, &QPushButton::clicked, this, &RouteDefaultsReview::accept);
        connect(safeOption, &QRadioButton::toggled, this, [this] {
            acknowledgement->setChecked(false);
            refresh();
        });
        connect(acknowledgement, &QCheckBox::toggled, this, [this] { refresh(); });
        refresh();
        resize(620, 500);
    }
    RouteDefaultsPolicy::Mode mode() const {
        return safeOption->isChecked() ? RouteDefaultsPolicy::Mode::SafeUpgrade : RouteDefaultsPolicy::Mode::ResetRoutes;
    }
    void accept() override {
        const bool safe = safeOption->isChecked();
        const auto &plan = safe ? safePlan : resetPlan;
        if (plan.changedFields.isEmpty()) {
            // Closing an unchanged review must never enter the caller's apply path.
            QDialog::reject();
            return;
        }
        if (!safe && !acknowledgement->isChecked()) return;
        QDialog::accept();
    }
private:
    RouteDefaultsPolicy::Preview safePlan, resetPlan;
    QRadioButton *safeOption, *resetOption;
    QCheckBox *acknowledgement;
    QLabel *summary, *changes, *noChanges, *note;
    QScrollArea *changesScroll;
    QPushButton *applyButton, *leaveButton;
    QString fieldLabel(const QString &field) const {
        const QList<QPair<QString, QString>> labels{
            {"direct_domain", tr("Сайты напрямую, без VPN")},
            {"proxy_domain", tr("Сайты через VPN")},
            {"block_domain", tr("Заблокированные сайты")},
            {"direct_ip", tr("IP-адреса напрямую, без VPN")},
            {"proxy_ip", tr("IP-адреса через VPN")},
            {"block_ip", tr("Заблокированные IP-адреса")},
            {"def_outbound", tr("Куда направлять остальной трафик")},
            {"custom", tr("Дополнительные правила и правила программ")},
            {"remote_dns", tr("DNS для сайтов через VPN")},
            {"remote_dns_strategy", tr("Выбор IPv4 и IPv6 для DNS через VPN")},
            {"direct_dns", tr("DNS для сайтов напрямую")},
            {"direct_dns_strategy", tr("Выбор IPv4 и IPv6 для DNS напрямую")},
            {"dns_routing", tr("Направление DNS-запросов по правилам")},
            {"use_dns_object", tr("Использование расширенной настройки DNS")},
            {"dns_object", tr("Расширенная настройка DNS")},
            {"dns_final_out", tr("Куда направлять остальные DNS-запросы")},
            {"sniffing_mode", tr("Определение сайтов и протоколов трафика")},
            {"domain_strategy", tr("Выбор IPv4 и IPv6 при подключении")},
            {"outbound_domain_strategy", tr("Выбор IPv4 и IPv6 для подключения к серверу")}};
        for (const auto &label : labels) if (label.first == field) return label.second;
        // A future field is visible as an additional change without presenting
        // internal keys or user-supplied configuration as product instructions.
        return tr("Дополнительный параметр схемы");
    }
    void refresh() {
        const bool safe = safeOption->isChecked();
        const auto &plan = safe ? safePlan : resetPlan;
        const bool hasChanges = !plan.changedFields.isEmpty();
        summary->setText(tr("Настроек к изменению: %1. Правила: добавить %2, удалить %3.\n%4")
            .arg(plan.changedFields.size()).arg(plan.addedRules).arg(plan.removedRules)
            .arg(safe ? tr("Обновятся узнаваемые устаревшие значения и добавятся недостающие правила. Приоритет ваших правил сохранится.")
                      : tr("Ваши списки сайтов и IP-адресов, DNS интернета и правила программ этой схемы будут заменены стандартными.")));
        QStringList fields;
        for (const auto &field : plan.changedFields) fields.append(QStringLiteral("• ") + fieldLabel(field));
        changes->setText(fields.join(QLatin1Char('\n')));
        summary->setVisible(hasChanges);
        changesScroll->setVisible(hasChanges);
        noChanges->setText(safe
            ? tr("Обновление не требуется. Текущие маршруты сохранятся. Нажмите «Готово», чтобы закрыть окно.")
            : tr("В этой схеме уже установлены стандартные маршруты. Повторный сброс не требуется. Нажмите «Готово», чтобы закрыть окно."));
        noChanges->setVisible(!hasChanges);
        acknowledgement->setVisible(!safe && hasChanges);
        acknowledgement->setEnabled(hasChanges);
        note->setVisible(hasChanges);
        leaveButton->setVisible(hasChanges);
        leaveButton->setDefault(hasChanges);
        applyButton->setText(hasChanges ? tr("Применить изменения") : tr("Готово"));
        applyButton->setDefault(!hasChanges);
        applyButton->setEnabled(!hasChanges || safe || acknowledgement->isChecked());
    }
};
} // namespace GreenRhythm
