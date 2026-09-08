/**
 * Порядок подъёма и снятия внешнего туннеля.
 *
 * ПОЧЕМУ ЭТОТ НАБОР ЕСТЬ. Внешний туннель (macOS, а также Windows и Linux без
 * «одного ядра») ведёт весь трафик в локальный SOCKS-порт основного ядра.
 * Поднятый до подключения или оставленный после отключения, он уводит машину в
 * закрытый порт — и снаружи это неотличимо от «туннель не работает». Правила
 * порядка вынесены в main/TunLifecycle.hpp чистыми функциями ровно затем,
 * чтобы их можно было проверить без мака, без ядра и без прав.
 *
 * Два случая здесь важнее прочих, потому что оба стоили бы запроса пароля не
 * вовремя: передача между профилями и падение ядра. В обоих туннель остаётся.
 *
 * Запуск: ninja tun_lifecycle_test && ./tun_lifecycle_test
 */

#include "main/TunLifecycle.hpp"

#include <cstdio>

static int checks = 0;
static int fails = 0;

static void is(const char *what, bool ok) {
    checks++;
    if (!ok) fails++;
    std::printf("  %s %s\n", ok ? "ok   " : "ПРОВАЛ", what);
}

int main() {
    using namespace GreenRhythm::TunLifecycle;

    // Подъём: только после того, как появился порт, и только один раз.
    is("до подключения внешний туннель ждёт локальный порт",
       !shouldStartExternal(true, false, false, false));
    is("после подключения профиля внешний туннель поднимается",
       shouldStartExternal(true, false, true, false));
    is("уже поднятый туннель второй раз не поднимается",
       !shouldStartExternal(true, false, true, true));
    is("внутренний туннель отдельным процессом не управляется",
       !shouldStartExternal(true, true, true, false));
    is("не выбранный туннель не поднимается",
       !shouldStartExternal(false, false, true, false));

    // Снятие: перед настоящим отключением — да; там, где ядро вернётся само
    // или где человека нет, — нет.
    is("настоящее отключение снимает внешний туннель",
       shouldStopExternalBeforeDisconnect(true, false, true, false, false));
    is("передача между профилями туннель оставляет",
       !shouldStopExternalBeforeDisconnect(true, false, true, true, false));
    is("падение ядра туннель оставляет: пароль посреди аварии не спрашивается",
       !shouldStopExternalBeforeDisconnect(true, false, true, false, true));
    is("нечего снимать, если помощник не запущен",
       !shouldStopExternalBeforeDisconnect(true, false, false, false, false));
    is("внутренний туннель отдельно не снимается",
       !shouldStopExternalBeforeDisconnect(true, true, true, false, false));

    std::printf("\nпроверок %d, провалов %d\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
