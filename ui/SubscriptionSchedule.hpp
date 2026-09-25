#pragma once

/**
 * Автообновление подписки одним числом — dataStore->sub_auto_update.
 *
 * Знак — включено ли, модуль — интервал в минутах: так число хранил старый
 * диалог (D_SAVE_INT_ENABLE), и так его читает таймер окна
 * (TM_auto_update_subsctiption_Reset_Minute), который заводится только от 30
 * минут. Правило одно на двоих — на переключатель страницы «Настройки» и на
 * поле интервала в «Дополнительных настройках»: иначе они разошлись бы в том,
 * что считать «включено».
 *
 * Сторож — test/SettingsPageTest.cpp.
 */
namespace GreenRhythm::SubscriptionSchedule {

    /// Чаще таймер окна не заводится: меньше 30 минут — всё равно что выключено.
    inline constexpr int kMinMinutes = 30;
    /// Интервал, когда помнить нечего: тот же, что у новых настроек и миграции.
    inline constexpr int kDefaultMinutes = 120;

    /** Работает ли автообновление — ровно тогда, когда таймер окна заведётся. */
    inline constexpr bool isOn(int stored) { return stored >= kMinMinutes; }

    /**
     * Что записать после переключателя. Интервал помнится и через выключение;
     * негодный (меньше 30) заменяется интервалом по умолчанию — иначе
     * включённый переключатель стоял бы при незаведённом таймере.
     */
    inline constexpr int toggled(int stored, bool on) {
        const int magnitude = stored < 0 ? -stored : stored;
        const int minutes = magnitude >= kMinMinutes ? magnitude : kDefaultMinutes;
        return on ? minutes : -minutes;
    }

    /**
     * Что записать после поля интервала. Включено ли — решает переключатель на
     * странице, и берётся из настроек в момент сохранения, а не при открытии
     * диалога: диалог немодальный, и прочитанное тогда могло устареть.
     */
    inline constexpr int withInterval(int stored, int minutes) {
        return isOn(stored) ? minutes : -minutes;
    }

} // namespace GreenRhythm::SubscriptionSchedule
