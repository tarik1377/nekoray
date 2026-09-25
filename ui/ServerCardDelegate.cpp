#include "ui/ServerCardDelegate.hpp"
#include "ui/Icons.hpp"
#include "ui/Palette.hpp"

#include <QAbstractItemModel>
#include <QPainter>
#include <QPainterPath>

namespace GreenRhythm {

    namespace {

        // Те же токены, что в теме и в оболочке, — из ui/Palette.hpp.
        const QColor kAccent(Palette::kAccent);
        const QColor kText(Palette::kText);
        const QColor kMuted(Palette::kMuted);
        const QColor kLine(Palette::kLine);
        const QColor kCard(Palette::kCard);
        const QColor kCardHover(Palette::kCardHover);
        const QColor kAmber(Palette::kAmber);
        const QColor kRed(Palette::kRed);

        constexpr int kRowHeight = 76;
        constexpr int kPad = 14;

        /**
         * Цвет задержки по величине.
         *
         * Пороги те же, по которым человек и судит: до 80 мс разницы не
         * чувствуется, до 200 играть можно, дальше — заметно.
         */
        QColor latencyColor(int ms) {
            if (ms <= 0) return kMuted;
            if (ms < 80) return kAccent;
            if (ms < 200) return kAmber;
            return kRed;
        }

        int parseLatency(const QString &text) {
            // «32 ms», «106 ms», «-1», пусто. Берём первое число; всё прочее —
            // «не измерено», и врать про это нельзя.
            const auto trimmed = text.trimmed();
            if (trimmed.isEmpty()) return 0;
            QString digits;
            for (const auto ch: trimmed) {
                if (ch.isDigit()) digits += ch;
                else if (!digits.isEmpty()) break;
                else if (ch == QChar('-')) return 0;
            }
            return digits.isEmpty() ? 0 : digits.toInt();
        }

    } // namespace

    ServerCardDelegate::ServerCardDelegate(int typeColumn, int addressColumn, int latencyColumn,
                                           QObject *parent)
        : QStyledItemDelegate(parent), typeColumn(typeColumn), addressColumn(addressColumn),
          latencyColumn(latencyColumn) {}

    QSize ServerCardDelegate::sizeHint(const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(kRowHeight);
        return size;
    }

    void ServerCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;

        // Карточка: скруглённая подложка с отступом от краёв строки. Отступ и
        // делает список списком карточек, а не сплошной таблицей.
        QRectF card = QRectF(option.rect).adjusted(6, 4, -6, -4);
        QPainterPath path;
        path.addRoundedRect(card, 10, 10);
        painter->fillPath(path, selected ? QColor(186, 214, 91, 28)
                                         : (hovered ? kCardHover : kCard));
        if (selected) {
            painter->setPen(QPen(kAccent, 1.5));
            painter->drawPath(path);
        }

        const auto *model = index.model();
        const int row = index.row();
        auto cell = [&](int column) {
            if (column < 0 || column >= model->columnCount()) return QString();
            return model->data(model->index(row, column)).toString().trimmed();
        };

        const QString name = index.data().toString().trimmed();
        const QString type = cell(typeColumn);
        const QString address = cell(addressColumn);
        const int ms = parseLatency(cell(latencyColumn));

        // Точка состояния. Зелёная у выбранного профиля — тот же признак, по
        // которому таблица и раньше помечала запущенный.
        const qreal dotX = card.left() + kPad + 4;
        const qreal midY = card.center().y();
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? kAccent : kLine);
        painter->drawPixmap(QPointF(card.left() + kPad, midY - 11), Icons::pixmap("gr-nav-servers", selected ? kAccent : kMuted, 22));

        // Пилюля задержки — справа, чтобы взгляд шёл «имя … насколько быстро».
        qreal rightEdge = card.right() - kPad;
        {
            const QString label = ms > 0 ? QStringLiteral("%1 мс").arg(ms)
                                         : QObject::tr("не проверен");
            QFont f = option.font;
            f.setPointSizeF(f.pointSizeF() * 0.9);
            f.setBold(ms > 0);
            painter->setFont(f);
            const QFontMetrics fm(f);
            const int w = fm.horizontalAdvance(label) + 20;
            const QRectF pill(rightEdge - w, midY - 11, w, 22);
            QPainterPath pillPath;
            pillPath.addRoundedRect(pill, 11, 11);
            const QColor c = latencyColor(ms);
            painter->fillPath(pillPath, QColor(c.red(), c.green(), c.blue(), 34));
            painter->setPen(c);
            painter->drawText(pill, Qt::AlignCenter, label);
            rightEdge = pill.left() - 12;
        }

        // Имя — крупно и белым. Подпись под ним — мелко и серым: протокол с
        // адресом человеку не нужны, но нужны поддержке, поэтому они не исчезают
        // совсем, а уходят на второй план.
        const qreal textLeft = card.left() + kPad + 36;
        const qreal textWidth = rightEdge - textLeft;

        QFont nameFont = option.font;
        nameFont.setBold(true);
        nameFont.setPointSizeF(nameFont.pointSizeF() * 1.05);
        painter->setFont(nameFont);
        painter->setPen(kText);
        const QFontMetrics nameFm(nameFont);
        painter->drawText(QRectF(textLeft, card.top() + 12, textWidth, nameFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          nameFm.elidedText(name, Qt::ElideRight, int(textWidth)));

        QStringList meta;
        if (!type.isEmpty()) meta << type;
        if (!address.isEmpty()) meta << address;
        if (!meta.isEmpty()) {
            QFont metaFont = option.font;
            metaFont.setPointSizeF(metaFont.pointSizeF() * 0.85);
            painter->setFont(metaFont);
            painter->setPen(kMuted);
            const QFontMetrics metaFm(metaFont);
            const QString line = meta.join(QStringLiteral("  ·  "));
            painter->drawText(QRectF(textLeft, card.top() + 12 + nameFm.height() + 2, textWidth,
                                     metaFm.height()),
                              Qt::AlignLeft | Qt::AlignVCenter,
                              metaFm.elidedText(line, Qt::ElideRight, int(textWidth)));
        }

        painter->restore();
    }

} // namespace GreenRhythm
