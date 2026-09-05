#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

/**
 * Значки из SVG, окрашенные в нужный цвет.
 *
 * ЗАЧЕМ СВОЙ СЛОЙ. Значки нарисованы линией цвета currentColor, чтобы один файл
 * служил и серому пункту, и зелёному выбранному. Но currentColor — это CSS, и
 * что с ним сделает QSvgRenderer, зависит от версии Qt и от того, куда значок
 * попал: кнопка навигации красится, QLabel с пиксмапом — нет, ячейка таблицы —
 * нет. Здесь цвет подставляется в текст SVG до отрисовки, и результат один и
 * тот же везде.
 *
 * Растр рисуется с учётом масштаба экрана: значок в 16 логических точек на
 * экране 150% — это 24 физические, иначе линия в 1.8 размывается в серую кашу.
 */
namespace GreenRhythm::Icons {

    /**
     * Растр значка.
     *
     * @param name  имя файла без расширения в res/icon (например, gr-routes)
     * @param color во что окрасить
     * @param size  логический размер стороны
     */
    QPixmap pixmap(const QString &name, const QColor &color, int size);

    /**
     * QIcon с состояниями: обычное — normal; наведение и включённое — active.
     *
     * Кнопка с setCheckable берёт QIcon::On, когда нажата, и QIcon::Active под
     * курсором — то же поведение, что у подписи рядом, и значок с ней не спорит.
     */
    QIcon icon(const QString &name, const QColor &normal, const QColor &active, int size);

} // namespace GreenRhythm::Icons
