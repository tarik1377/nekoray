#pragma once

#include <QStyledItemDelegate>

namespace GreenRhythm {

    /**
     * Строка списка серверов, нарисованная карточкой.
     *
     * ЗАЧЕМ ДЕЛЕГАТ, А НЕ ДРУГОЙ ВИДЖЕТ. Карточки просятся списком виджетов, но
     * таблица несёт на себе сортировку по колонкам, перетаскивание строк для
     * смены порядка, контекстное меню, множественное выделение, поиск и запуск
     * двойным щелчком — всё это привязано к proxyListTable и к его модели.
     * Заменив таблицу, пришлось бы переносить и это, а такой переезд теряет
     * что-нибудь молча. Делегат меняет ТОЛЬКО отрисовку: модель, отбор и все
     * жесты остаются теми же.
     *
     * ЧТО РИСУЕТСЯ. Слева точка состояния, дальше крупное имя, под ним мелким и
     * серым — протокол и адрес. Справа пилюля задержки, покрашенная по величине.
     * Прежде всё это лежало шестью колонками, где «Тип» у всех строк одинаковый,
     * а имя обрезалось до «Germanyyy…», потому что ширину делили поровну.
     *
     * Данные берутся из соседних ячеек той же строки: колонки скрыты, но не
     * удалены, поэтому и поиск по адресу продолжает работать, и рисовать есть из
     * чего.
     */
    class ServerCardDelegate : public QStyledItemDelegate {
        Q_OBJECT

    public:
        /**
         * @param typeColumn    колонка с протоколом
         * @param addressColumn колонка с адресом
         * @param latencyColumn колонка с задержкой
         */
        explicit ServerCardDelegate(int typeColumn, int addressColumn, int latencyColumn,
                                    QObject *parent = nullptr);

        void paint(QPainter *painter, const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    private:
        int typeColumn;
        int addressColumn;
        int latencyColumn;
    };

} // namespace GreenRhythm
