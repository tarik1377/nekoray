set(PLATFORM_SOURCES 3rdparty/WinCommander.cpp sys/windows/guihelper.cpp sys/windows/MiniDump.cpp sys/windows/SealedStore_win.cpp)
# crypt32 — DPAPI для запечатки реквизитов доступа (main/SealedStore.hpp)
# Отдельными переменными: тестовой цели нужна только запечатка, без GUI-частей.
set(SEALED_STORE_SOURCE sys/windows/SealedStore_win.cpp)
set(SEALED_STORE_LIBRARIES crypt32)
set(PLATFORM_LIBRARIES wininet wsock32 ws2_32 user32 rasapi32 iphlpapi crypt32)

include(cmake/windows/generate_product_version.cmake)
# Explorer and installation diagnostics must report the same version as the GUI.
if (NOT DEFINED NKR_VERSION)
    file(STRINGS "${CMAKE_SOURCE_DIR}/nekoray_version.txt" NKR_VERSION LIMIT_COUNT 1)
endif ()
if (NOT NKR_VERSION MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)(\\.([0-9]+))?(-[A-Za-z0-9.-]+)?$")
    message(FATAL_ERROR "NKR_VERSION must contain a numeric Windows product version: ${NKR_VERSION}")
endif ()
set(GR_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(GR_VERSION_MINOR "${CMAKE_MATCH_2}")
set(GR_VERSION_PATCH "${CMAKE_MATCH_3}")
set(GR_VERSION_REVISION "${CMAKE_MATCH_5}")
if (GR_VERSION_REVISION STREQUAL "")
    set(GR_VERSION_REVISION 0)
endif ()
generate_product_version(
        QV2RAY_RC
        ICON "${CMAKE_SOURCE_DIR}/res/greenrhythm.ico"
        NAME "GreenRhythm"
        BUNDLE "GreenRhythm"
        COMPANY_NAME "GreenRhythm"
        COMPANY_COPYRIGHT "(C) 2025-2026 GreenRhythm. GPL-3.0. Based on nekoray by MatsuriDayo."
        FILE_DESCRIPTION "GreenRhythm"
        VERSION_MAJOR "${GR_VERSION_MAJOR}"
        VERSION_MINOR "${GR_VERSION_MINOR}"
        VERSION_PATCH "${GR_VERSION_PATCH}"
        VERSION_REVISION "${GR_VERSION_REVISION}"
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
