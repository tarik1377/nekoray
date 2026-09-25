#pragma once

void AutoRun_SetEnabled(bool enable);

bool AutoRun_IsEnabled();

/**
 * Автозапуск идёт с правами администратора — задачей планировщика (Windows,
 * sys/AutoRunTask.hpp). Туннелю при входе они нужны; из раздела Run программа
 * стартует без них. На других системах — всегда нет.
 */
bool AutoRun_IsElevated();
