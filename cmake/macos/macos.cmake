# Платформенные переменные для macOS.
#
# ПОЧЕМУ ОТДЕЛЬНЫЙ ФАЙЛ, А НЕ ВЕТКА В linux.cmake. До него мак попадал в
# `else()` и собирался как Linux: тянул sys/linux/LinuxCap.cpp (getcap, setcap,
# pkexec — ничего этого на маке нет) и линуксовую запечатку, которая выводит
# ключ из /etc/machine-id. Второе хуже первого: файла нет, ключ выходит пустым,
# запись реквизитов молча отвечает отказом, и активация не завершается никогда.

set(PLATFORM_SOURCES
        sys/macos/SealedStore_macos.cpp
        sys/macos/MacProxyController.cpp
        sys/macos/PacServer.cpp
        sys/macos/PacBuilder.cpp
)
set(PLATFORM_LIBRARIES)

# Отдельными переменными: тестовой цели нужна только запечатка, без GUI-частей.
set(SEALED_STORE_SOURCE sys/macos/SealedStore_macos.cpp)
set(SEALED_STORE_LIBRARIES)

# НАСТРОЙКИ ЛЕЖАТ РЯДОМ С ПОЛЬЗОВАТЕЛЕМ, А НЕ ВНУТРИ .app — ОБЯЗАТЕЛЬНО.
#
# Без этого определения приложение пишет конфигурацию в свой каталог. На маке
# это не «неаккуратно», а неработоспособно: скачанное из интернета приложение
# система помечает карантином и запускает из ТОЛЬКО ДЛЯ ЧТЕНИЯ копии в
# /private/var/folders/.../AppTranslocation/. Запись туда падает, и человек
# видит приложение, которое не запоминает вообще ничего — ни серверов, ни
# настроек, ни активации.
add_compile_definitions(NKR_CPP_USE_APPDATA)

# Приложение собирается бандлом: иначе система не покажет его в Программах, не
# даст иконку и не примет ссылки greenrhythm://.
set(GUI_TYPE MACOSX_BUNDLE)
