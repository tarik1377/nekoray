#pragma once

/**
 * Цвета «лесного» вида — одна копия для всего кода.
 *
 * ЗАЧЕМ. 25.09.2026 владелец принял вид из PR #38 и попросил сделать так же
 * везде. Цвета при этом жили копиями — в оболочке, карточке сервера, журнале,
 * трее, пилюле состояния, окне первого запуска, панели «Зелёный Ритм», — и после
 * смены палитры прежний зелёный акцент остался в двенадцати местах. Заметить это
 * можно было только по снимку. Теперь код берёт цвета отсюда, а вторая и
 * последняя копия — таблица стилей res/theme/feiyangqingyun/qss/modern.css. Что
 * они не разошлись, проверяет palette_test.
 *
 * ЛИТЕРАЛОМ, А НЕ ПАЛИТРОЙ: таблица стилей Qt перекрывает QPalette, и цвет,
 * выставленный палитрой, молча не применяется.
 */
namespace GreenRhythm::Palette {

    inline constexpr const char *kAccent = "#bad65b";    ///< подключено, выбранное, основная кнопка
    inline constexpr const char *kAccentDim = "#a9c64b"; ///< акцент под курсором и нажатием
    inline constexpr const char *kOnAccent = "#08170c";  ///< текст на акцентной заливке

    // Три яруса: колонка темнее страницы, карточка светлее. Глубина читается без рамок.
    inline constexpr const char *kSidebar = "#0a140d";
    inline constexpr const char *kSurface = "#0c1710";
    inline constexpr const char *kSurfaceUp = "#17251a";
    inline constexpr const char *kCard = "#122116";      ///< карточка сервера в списке
    inline constexpr const char *kCardHover = "#21331f";

    inline constexpr const char *kLine = "#293923";      ///< линии и выключенный переключатель
    inline constexpr const char *kDim = "#4f6545";       ///< «не запущено», «не проверен»
    inline constexpr const char *kText = "#e8eee4";
    inline constexpr const char *kMuted = "#99a795";     ///< подписи второго плана, «напрямую»

    inline constexpr const char *kAmber = "#e3a008";     ///< подключено без трафика, задержка средняя
    inline constexpr const char *kRed = "#e5484d";       ///< отказ, блокировка
    inline constexpr const char *kBlue = "#4c9aff";      ///< системный прокси
    inline constexpr const char *kReserve = "#3b82f6";   ///< резервное подключение: запасной путь, не «всё как задумано»

} // namespace GreenRhythm::Palette
