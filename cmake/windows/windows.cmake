set(PLATFORM_SOURCES 3rdparty/WinCommander.cpp sys/windows/guihelper.cpp sys/windows/MiniDump.cpp sys/windows/SealedStore_win.cpp)
# crypt32 — DPAPI для запечатки реквизитов доступа (main/SealedStore.hpp)
# Отдельными переменными: тестовой цели нужна только запечатка, без GUI-частей.
set(SEALED_STORE_SOURCE sys/windows/SealedStore_win.cpp)
set(SEALED_STORE_LIBRARIES crypt32)
set(PLATFORM_LIBRARIES wininet wsock32 ws2_32 user32 rasapi32 iphlpapi crypt32)

include(cmake/windows/generate_product_version.cmake)
generate_product_version(
        QV2RAY_RC
        ICON "${CMAKE_SOURCE_DIR}/res/greenrhythm.ico"
        NAME "GreenRhythm"
        BUNDLE "GreenRhythm"
        COMPANY_NAME "GreenRhythm"
        COMPANY_COPYRIGHT "(C) 2025-2026 GreenRhythm. GPL-3.0. Based on nekoray by MatsuriDayo."
        FILE_DESCRIPTION "GreenRhythm"
)
add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)
set(GUI_TYPE WIN32)
if (MINGW)
    if (NOT DEFINED MinGW_ROOT)
        set(MinGW_ROOT "C:/msys64/mingw64")
    endif ()
else ()
    add_compile_options("/utf-8")
    add_compile_options("/std:c++17")
    add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS)
endif ()
