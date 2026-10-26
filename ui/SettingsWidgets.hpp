#pragma once

#include "ui/Icons.hpp"
#include "ui/Palette.hpp"
#include <QBoxLayout>
#include <QEvent>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QWidget>

namespace GreenRhythm {

inline bool settingsMatch(QString text, QString query) {
    text = text.toCaseFolded().replace(QChar(0x451), QChar(0x435));
    query = query.simplified().toCaseFolded().replace(QChar(0x451), QChar(0x435));
    for (const auto &word : query.split(' ', Qt::SkipEmptyParts))
        if (!text.contains(word)) return false;
    return true;
}

inline QString settingsButtonStyle(bool primary = false) {
    return QStringLiteral(
        "QPushButton { min-width:0; min-height:32px; padding:0 12px; border:1px solid %1;"
        " border-radius:8px; background:%2; color:%3; }"
        "QPushButton:hover { background:%4; border-color:%5; }"
        "QPushButton:focus { border:1px solid %5; }"
        "QPushButton:checked { background:%4; color:%5; border-color:%5; }"
        "QPushButton:disabled { background:%6; color:%7; border-color:%1; }")
        .arg(primary ? Palette::kAccent : Palette::kLine,
             primary ? Palette::kAccent : Palette::kSurface,
             primary ? Palette::kOnAccent : Palette::kText,
             primary ? Palette::kAccentDim : Palette::kCardHover,
             Palette::kAccent, Palette::kSurface, Palette::kMuted);
}

// Presentation only: existing buttons keep their signal connections and state.
class SettingsCard : public QWidget {
public:
    SettingsCard(const QString &title, const QString &icon, QWidget *parent = nullptr) : QWidget(parent) {
        setProperty("settingsCard", true);
        setAttribute(Qt::WA_StyledBackground, true);
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setStyleSheet(QStringLiteral("QWidget[settingsCard=\"true\"] { background:%1; border:1px solid %2; border-radius:12px; }")
                          .arg(Palette::kSurfaceUp, Palette::kLine));
        box = new QVBoxLayout(this);
        box->setContentsMargins(16, 14, 16, 14);
        box->setSpacing(10);
        header = new QHBoxLayout;
        header->setSpacing(9);
        auto *glyph = new QLabel(this);
        glyph->setPixmap(Icons::pixmap(icon, QColor(Palette::kAccent), 18));
        header->addWidget(glyph);
        auto *heading = new QLabel(title, this);
        heading->setTextFormat(Qt::PlainText);
        heading->setWordWrap(true);
        auto font = heading->font(); font.setWeight(QFont::DemiBold); heading->setFont(font);
        header->addWidget(heading, 1);
        box->addLayout(header);
        detail = new QLabel(this);
        detail->setTextFormat(Qt::PlainText);
        detail->setWordWrap(true);
        detail->setMinimumWidth(0);
        detail->setStyleSheet(QStringLiteral("color:%1; background:transparent; border:none;").arg(Palette::kMuted));
        font = detail->font(); font.setPointSizeF(font.pointSizeF() * 0.9); detail->setFont(font);
        box->addWidget(detail);
        box->addStretch(1);
        actions = new QBoxLayout(QBoxLayout::LeftToRight);
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(8);
        actions->addStretch();
        box->addLayout(actions);
    }
    QPushButton *addAction(const QString &name, const QString &title, bool primary = false) {
        auto *button = new QPushButton(title, this);
        button->setObjectName(name);
        button->setAccessibleName(title);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(settingsButtonStyle(primary));
        auto font = button->font(); font.setPointSizeF(font.pointSizeF() * 0.93); button->setFont(font);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        actions->insertWidget(actions->count() - 1, button);
        buttons.append(button);
        return button;
    }
    bool filter(const QString &query) {
        QString text;
        for (auto *label : findChildren<QLabel *>()) text += ' ' + label->text();
        for (auto *button : findChildren<QPushButton *>()) text += ' ' + button->text() + ' ' + button->accessibleName();
        const bool match = settingsMatch(text, query);
        setVisible(match);
        return match;
    }
    QVBoxLayout *box = nullptr;
    QHBoxLayout *header = nullptr;
    QLabel *detail = nullptr;
    QSize minimumSizeHint() const override { return {0, QWidget::minimumSizeHint().height()}; }
protected:
    void resizeEvent(QResizeEvent *event) override { QWidget::resizeEvent(event); adaptActions(); }
    bool event(QEvent *event) override {
        const bool handled = QWidget::event(event);
        if (event->type() == QEvent::LayoutRequest && actions) adaptActions();
        return handled;
    }
private:
    void adaptActions() {
        int required = 0, count = 0;
        for (auto *button : buttons) if (!button->isHidden()) { required += button->sizeHint().width(); ++count; }
        required += qMax(0, count - 1) * actions->spacing();
        const auto direction = required <= width() - 32 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
        if (actions->direction() != direction) actions->setDirection(direction);
        const auto policy = direction == QBoxLayout::LeftToRight ? QSizePolicy::Maximum : QSizePolicy::Expanding;
        for (auto *button : buttons)
            if (button->sizePolicy().horizontalPolicy() != policy) button->setSizePolicy(policy, QSizePolicy::Fixed);
    }
    QBoxLayout *actions = nullptr;
    QList<QPushButton *> buttons;
};

class SettingsSection : public QWidget {
public:
    explicit SettingsSection(const QString &title, QWidget *parent = nullptr) : QWidget(parent), sectionTitle(title) {
        setObjectName("grSettingsSection");
        auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0, 0, 0, 0); outer->setSpacing(8);
        auto *heading = new QLabel(title, this);
        auto font = heading->font(); font.setWeight(QFont::DemiBold); font.setPointSizeF(font.pointSizeF() * 0.86);
        heading->setFont(font);
        heading->setStyleSheet(QStringLiteral("color:%1; padding-left:2px;").arg(Palette::kMuted));
        outer->addWidget(heading);
        body = new QWidget(this); body->setProperty("settingsSectionBody", true);
        body->setAttribute(Qt::WA_StyledBackground, true);
        body->setStyleSheet(QStringLiteral("QWidget[settingsSectionBody=\"true\"] { background:%1; border:1px solid %2; border-radius:12px; }")
                               .arg(Palette::kSurfaceUp, Palette::kLine));
        rowsLayout = new QVBoxLayout(body); rowsLayout->setContentsMargins(0, 0, 0, 0); rowsLayout->setSpacing(0);
        outer->addWidget(body);
    }
    QLabel *addRow(const QString &title, const QString &description, const QString &icon, QPushButton *button) {
        auto *separator = new QFrame(body);
        separator->setFixedHeight(1);
        separator->setStyleSheet(QStringLiteral("background:%1; margin:0 14px;").arg(Palette::kLine));
        rowsLayout->addWidget(separator);
        separator->setVisible(!rows.isEmpty());
        auto *row = new QWidget(body); row->setObjectName("grSettingRow");
        row->setStyleSheet("QWidget#grSettingRow { background:transparent; border:none; }");
        auto *layout = new QHBoxLayout(row); layout->setContentsMargins(16, 13, 16, 13); layout->setSpacing(12);
        auto *glyph = new QLabel(row); glyph->setPixmap(Icons::pixmap(icon, QColor(Palette::kMuted), 19));
        layout->addWidget(glyph);
        auto *labels = new QVBoxLayout; labels->setSpacing(4);
        auto *heading = new QLabel(title, row); heading->setTextFormat(Qt::PlainText); heading->setWordWrap(true);
        auto font = heading->font(); font.setWeight(QFont::DemiBold); heading->setFont(font);
        labels->addWidget(heading);
        auto *detail = new QLabel(description, row); detail->setTextFormat(Qt::PlainText); detail->setWordWrap(true);
        font = detail->font(); font.setPointSizeF(font.pointSizeF() * 0.9); detail->setFont(font);
        detail->setStyleSheet(QStringLiteral("color:%1;").arg(Palette::kMuted));
        labels->addWidget(detail);
        layout->addLayout(labels, 1);
        if (!button->isCheckable()) button->setStyleSheet(settingsButtonStyle());
        if (button->accessibleName().isEmpty()) button->setAccessibleName(title);
        button->setCursor(Qt::PointingHandCursor);
        layout->addWidget(button);
        rowsLayout->addWidget(row);
        rows.append({row, separator, heading, detail, button, labels, nullptr});
        return detail;
    }
    QLabel *addStatus(QPushButton *button) {
        for (auto &row : rows) if (row.button == button) {
            auto *status = new QLabel(row.widget);
            status->setWordWrap(true); status->setTextFormat(Qt::PlainText);
            status->setStyleSheet(QStringLiteral("color:%1;").arg(Palette::kMuted));
            auto font = status->font(); font.setPointSizeF(font.pointSizeF() * 0.9); status->setFont(font);
            row.labels->addWidget(status); row.status = status; status->hide();
            return status;
        }
        // Platforms without this optional feature retain a harmless hidden label.
        auto *status = new QLabel(body); status->hide(); rowsLayout->addWidget(status); return status;
    }
    int filter(const QString &query) {
        int visible = 0;
        for (const auto &row : rows) {
            const auto text = row.heading->text() + ' ' + row.detail->text() + ' ' + row.button->accessibleName()
                + (row.status && !row.status->isHidden() ? ' ' + row.status->text() : QString());
            const bool wholeSection = !query.trimmed().isEmpty() && sectionTitle.compare(query.simplified(), Qt::CaseInsensitive) == 0;
            const bool match = wholeSection || settingsMatch(text, query);
            row.widget->setVisible(match);
            row.separator->setVisible(match && visible > 0);
            if (match) ++visible;
        }
        setVisible(visible > 0);
        return visible;
    }
private:
    struct Row { QWidget *widget; QWidget *separator; QLabel *heading; QLabel *detail; QPushButton *button; QVBoxLayout *labels; QLabel *status; };
    QString sectionTitle;
    QWidget *body = nullptr;
    QVBoxLayout *rowsLayout = nullptr;
    QList<Row> rows;
};
} // namespace GreenRhythm
