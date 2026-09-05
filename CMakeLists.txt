cmake_minimum_required(VERSION 3.16) # target_precompile_headers needs 3.16+

project(greenrhythm VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# WINDOWS PDB FILE
if (WIN32)
    if (MSVC)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
    endif ()
endif ()

# Find Qt
if (NOT QT_VERSION_MAJOR)
    set(QT_VERSION_MAJOR 5)
endif ()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Svg LinguistTools)

if (NKR_CROSS)
    set_property(TARGET Qt5::moc PROPERTY IMPORTED_LOCATION /usr/bin/moc)
    set_property(TARGET Qt5::uic PROPERTY IMPORTED_LOCATION /usr/bin/uic)
    set_property(TARGET Qt5::rcc PROPERTY IMPORTED_LOCATION /usr/bin/rcc)
    set_property(TARGET Qt5::lrelease PROPERTY IMPORTED_LOCATION /usr/bin/lrelease)
    set_property(TARGET Qt5::lupdate PROPERTY IMPORTED_LOCATION /usr/bin/lupdate)
endif ()

#### Platform Variables ####
# Мак — ОТДЕЛЬНОЙ веткой, а не через else(). До этого он собирался как Linux и
# тянул getcap/setcap/pkexec, которых там нет, и запечатку от /etc/machine-id,
# которого там тоже нет: ключ выходил пустым, и реквизиты не сохранялись молча.
if (WIN32)
    include("cmake/windows/windows.cmake")
elseif (APPLE)
    include("cmake/macos/macos.cmake")
else ()
    include("cmake/linux/linux.cmake")
endif ()

#### default prefix path ####

if (NOT NKR_LIBS)
    if (NKR_PACKAGE)
        list(APPEND NKR_LIBS ${CMAKE_SOURCE_DIR}/libs/deps/package)
    else ()
        list(APPEND NKR_LIBS ${CMAKE_SOURCE_DIR}/libs/deps/built)
    endif ()
endif ()

if (NOT NKR_DISABLE_LIBS)
    list(APPEND CMAKE_PREFIX_PATH ${NKR_LIBS})
endif ()

message("[CMAKE_PREFIX_PATH] ${CMAKE_PREFIX_PATH}")

# for some cross toolchain
list(APPEND CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH})
message("[CMAKE_FIND_ROOT_PATH] ${CMAKE_FIND_ROOT_PATH}")

#### NKR ####

include("cmake/print.cmake")
include("cmake/nkr.cmake")

find_package(Threads)

#### NKR EXTERNAL ####

if (NKR_NO_EXTERNAL)
    set(NKR_NO_GRPC 1)
    set(NKR_NO_YAML 1)
    set(NKR_NO_ZXING 1)
    set(NKR_NO_QHOTKEY 1)
endif ()

# grpc
if (NKR_NO_GRPC)
    nkr_add_compile_definitions(NKR_NO_GRPC)
else ()
    # My proto
    include("cmake/myproto.cmake")
    list(APPEND NKR_EXTERNAL_TARGETS myproto)
endif ()

# yaml-cpp
if (NKR_NO_YAML)
    nkr_add_compile_definitions(NKR_NO_YAML)
else ()
    find_package(yaml-cpp CONFIG REQUIRED) # only Release is built
    list(APPEND NKR_EXTERNAL_TARGETS yaml-cpp)
endif ()

# zxing-cpp
if (NKR_NO_ZXING)
    nkr_add_compile_definitions(NKR_NO_ZXING)
else ()
    find_package(ZXing CONFIG REQUIRED)
    list(APPEND NKR_EXTERNAL_TARGETS ZXing::ZXing)
endif ()

# QHotkey (static submodule)
if (NKR_NO_QHOTKEY)
    nkr_add_compile_definitions(NKR_NO_QHOTKEY)
else ()
    set(QHOTKEY_INSTALL OFF)
    set(BUILD_SHARED_LIBS OFF)

    # QHotkey — ПОДМОДУЛЬ на upstream, и его CMakeLists начинается с
    # cmake_minimum_required(VERSION 3.1). CMake 4 считает это не устаревшим, а
    # ОШИБКОЙ: «Compatibility with CMake < 3.5 has been removed». Конфигурация
    # падает целиком, и падает она на чужом файле, который мы не вправе править
    # отсюда — у подмодуля отслеживается только указатель на коммит.
    #
    # CMAKE_POLICY_VERSION_MINIMUM — штатный выход, заведённый в CMake 3.31
    # ровно для этого случая. На CMake старше он просто неизвестная переменная и
    # не делает ничего, поэтому строка безопасна на всех версиях.
    #
    # Ставится ТОЛЬКО вокруг подмодуля и снимается сразу после: глобально она
    # ослабила бы проверку политик всему проекту, а нам нужен один чужой файл.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(3rdparty/QHotkey)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
    list(APPEND NKR_EXTERNAL_TARGETS qhotkey)
endif ()

#### debug print ####

if (DBG_CMAKE)
    print_all_variables()
    print_target_properties(myproto)
    print_target_properties(yaml-cpp)
    print_target_properties(ZXing::ZXing)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CMAKE_COMMAND} -E time")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK "${CMAKE_COMMAND} -E time")
endif ()

# Sources
set(PROJECT_SOURCES
        ${PLATFORM_SOURCES}

        main/main.cpp
        main/NekoGui.cpp
        main/NekoGui_Utils.cpp
        main/HTTPRequestHelper.cpp
        main/RelayTrace.cpp
        main/RelayTrace.hpp
        main/DeviceCredentials.cpp
        main/DeviceCredentials.hpp
        main/SealedStore.hpp
        main/RelayActivation.cpp
        ui/dialog_relay_activate.cpp
        ui/dialog_relay_activate.h
        ui/dialog_relay_activate.ui
        main/RelayActivationParse.cpp
        main/RelayActivation.hpp
        main/RelayComponent.cpp
        main/RelayComponentParse.cpp
        main/RelayComponent.hpp

        3rdparty/base64.cpp
        3rdparty/qrcodegen.cpp
        3rdparty/QtExtKeySequenceEdit.cpp

        3rdparty/qv2ray/v2/ui/QvAutoCompleteTextEdit.cpp
        3rdparty/qv2ray/v2/components/proxy/QvProxyConfigurator.cpp
        3rdparty/qv2ray/v2/ui/widgets/common/QJsonModel.cpp
        3rdparty/qv2ray/v2/ui/widgets/editors/w_JsonEditor.cpp
        3rdparty/qv2ray/v2/ui/widgets/editors/w_JsonEditor.hpp
        3rdparty/qv2ray/v2/ui/widgets/editors/w_JsonEditor.ui

        3rdparty/qv2ray/v3/components/GeositeReader/GeositeReader.cpp
        3rdparty/qv2ray/v3/components/GeositeReader/picoproto.cpp

        rpc/gRPC.cpp

        db/Database.cpp
        db/traffic/TrafficLooper.cpp
        db/ProfileFilter.cpp
        db/ConfigBuilder.cpp

        fmt/AbstractBean.cpp
        fmt/Bean2CoreObj_box.cpp
        fmt/Bean2External.cpp
        fmt/Bean2Link.cpp
        fmt/Link2Bean.cpp
        fmt/ChainBean.hpp # translate
        fmt/RelayBean.hpp # translate

        sub/GroupUpdater.cpp

        sys/ExternalProcess.cpp
        sys/AutoRun.cpp
        sys/ForeignTunnels.cpp
        ui/dialog_macos_mode.cpp

        ui/ThemeManager.cpp
        ui/Icon.cpp

        ui/mainwindow_grpc.cpp
        ui/mainwindow.cpp
        # Части главного окна. Выделены из mainwindow.cpp: он дорос до 4600
        # строк при принятом здесь пределе в 800. Переехали тела методов,
        # объявления остались в mainwindow.h.
        ui/NetworkRepair.cpp
        ui/Diagnostics.cpp
        ui/Autopilot.cpp
        ui/Onboarding.cpp
        ui/LogAndConnections.cpp
        ui/SubscriptionUi.cpp
        ui/mainwindow_common.hpp
        ui/mainwindow.h
        ui/mainwindow.ui

        ui/edit/dialog_edit_profile.h
        ui/edit/dialog_edit_profile.cpp
        ui/edit/dialog_edit_profile.ui
        ui/edit/dialog_edit_group.h
        ui/edit/dialog_edit_group.cpp
        ui/edit/dialog_edit_group.ui

        ui/edit/edit_chain.h
        ui/edit/edit_chain.cpp
        ui/edit/edit_chain.ui
        ui/edit/edit_socks_http.h
        ui/edit/edit_socks_http.cpp
        ui/edit/edit_socks_http.ui
        ui/edit/edit_shadowsocks.h
        ui/edit/edit_shadowsocks.cpp
        ui/edit/edit_shadowsocks.ui
        ui/edit/edit_vmess.h
        ui/edit/edit_vmess.cpp
        ui/edit/edit_vmess.ui
        ui/edit/edit_trojan_vless.h
        ui/edit/edit_trojan_vless.cpp
        ui/edit/edit_trojan_vless.ui

        ui/edit/edit_naive.h
        ui/edit/edit_naive.cpp
        ui/edit/edit_relay.cpp
        ui/edit/edit_relay.h
        ui/edit/edit_relay.ui
        ui/edit/edit_naive.ui

        ui/edit/edit_quic.h
        ui/edit/edit_quic.cpp
        ui/edit/edit_quic.ui

        ui/edit/edit_custom.h
        ui/edit/edit_custom.cpp
        ui/edit/edit_custom.ui

        ui/dialog_basic_settings.cpp
        ui/dialog_basic_settings.h
        ui/dialog_basic_settings.ui

        ui/dialog_manage_groups.cpp
        ui/dialog_manage_groups.h
        ui/dialog_manage_groups.ui

        main/RunningPrograms.cpp

        # Модуль обхода. Каталог стратегий и построитель плана — чистые, без
        # системы; DpiBundle и DpiModule трогают Windows и внутри себя закрыты
        # Q_OS_WIN, чтобы дерево оставалось собираемым под macOS.
        dpi/DpiCatalog.cpp
        dpi/DpiPlan.cpp
        dpi/DpiBundle.cpp
        dpi/DpiModule.cpp
        dpi/DpiModule.hpp

        ui/dialog_whatbroke.cpp
        ui/ServerCardDelegate.cpp
        ui/MainShell.cpp
        main/PortHealth.cpp
        main/ProgramTrouble.cpp
        main/Interference.cpp
        main/ConnectionRow.cpp
        main/AppFont.cpp
        ui/dialog_interference.cpp
        ui/dialog_greenrhythm.cpp
        ui/dialog_manage_routes.cpp
        ui/dialog_manage_routes.h
        ui/dialog_manage_routes.ui

        ui/dialog_vpn_settings.cpp
        ui/dialog_vpn_settings.h
        ui/dialog_vpn_settings.ui

        ui/dialog_hotkey.cpp
        ui/dialog_hotkey.h
        ui/dialog_hotkey.ui

        ui/widget/ProxyItem.cpp
        ui/widget/ProxyItem.h
        ui/widget/ProxyItem.ui
        ui/widget/GroupItem.cpp
        ui/widget/GroupItem.h
        ui/widget/GroupItem.ui

        res/neko.qrc
        res/theme/feiyangqingyun/qss.qrc
        ${QV2RAY_RC}
)

# Qt exe
if (${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(greenrhythm
            MANUAL_FINALIZATION
            ${PROJECT_SOURCES}
    )
    # Define target properties for Android with Qt 6 as:
    #    set_property(TARGET greenrhythm APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
    #                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
    # For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else ()
    if (ANDROID)
        add_library(greenrhythm SHARED
                ${PROJECT_SOURCES}
        )
        # Define properties for Android with Qt 5 after find_package() calls as:
        #    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else ()
        add_executable(greenrhythm
                ${PROJECT_SOURCES}
        )
    endif ()
endif ()

# Target

set_property(TARGET greenrhythm PROPERTY AUTOUIC ON)
set_property(TARGET greenrhythm PROPERTY AUTOMOC ON)
set_property(TARGET greenrhythm PROPERTY AUTORCC ON)

set_target_properties(greenrhythm PROPERTIES
        WIN32_EXECUTABLE TRUE
)

if (APPLE)
    # Версия без ведущей «v»: CFBundleVersion система ждёт числами и точками, и
    # «v1.4.5» она примет молча, но при первой же попытке заверить сборку
    # выяснится, что версия нечитаемая. Проще не заводить эту мину сейчас.
    string(REGEX REPLACE "^v" "" NKR_VERSION_PLAIN "${NKR_VERSION}")

    # Бандл со своим Info.plist. Схема greenrhythm:// разбирается в коде давно
    # (main.cpp читает её из аргументов), но система не отдаст нам ссылку, пока
    # приложение не объявит, что умеет её принимать, — а объявляется это только
    # здесь.
    set_target_properties(greenrhythm PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/macos/Info.plist.in
            MACOSX_BUNDLE_BUNDLE_NAME "GreenRhythm"
            # Имя файла значка. Сам файл кладёт libs/deploy_macos.sh — он
            # собирает его из res/public/greenrhythm.png системными sips и
            # iconutil, поэтому двоичного .icns в репозитории нет. Без этого
            # поля CFBundleIconFile выходит ПУСТЫМ, и человек видит в Finder,
            # Launchpad и Spotlight белый лист вместо значка — для
            # неподписанного приложения это та деталь, после которой файл
            # удаляют, не открывая.
            MACOSX_BUNDLE_ICON_FILE "greenrhythm.icns"
            MACOSX_BUNDLE_GUI_IDENTIFIER "ru.verdantvibe.greenrhythm"
            MACOSX_BUNDLE_BUNDLE_VERSION "${NKR_VERSION_PLAIN}"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${NKR_VERSION_PLAIN}"
    )
endif ()

# Precompiled header — speeds up incremental rebuilds (heavy Qt includes compiled once).
target_precompile_headers(greenrhythm PRIVATE main/pch.hpp)

# Target Source Translations

set(TS_FILES
        translations/zh_CN.ts
        translations/fa_IR.ts
        translations/ru_RU.ts
)
set(LUPDATE_OPTIONS
        -locations none -no-obsolete
)
if (${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_lupdate(greenrhythm TS_FILES ${TS_FILES} OPTIONS ${LUPDATE_OPTIONS})
    qt_add_lrelease(greenrhythm TS_FILES ${TS_FILES} QM_FILES_OUTPUT_VARIABLE QM_FILES)
else ()
    qt5_create_translation(QM_FILES ${PROJECT_SOURCES} ${TS_FILES} OPTIONS ${LUPDATE_OPTIONS})
endif ()
configure_file(translations/translations.qrc ${CMAKE_BINARY_DIR} COPYONLY)
target_sources(greenrhythm PRIVATE ${CMAKE_BINARY_DIR}/translations.qrc)

# Target Link

target_link_libraries(greenrhythm PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg
        Threads::Threads
        ${NKR_EXTERNAL_TARGETS}
        ${PLATFORM_LIBRARIES}
)

if (QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(greenrhythm)
endif ()

# --- Проверки ---------------------------------------------------------------
# Отдельная цель, а не часть greenrhythm: набор линкуется с Qt6::Core, а не с
# Widgets, собирается за секунды и в выпуск не попадает. Собирать и запускать:
#     ninja relaytrace_test && ./relaytrace_test
#
# Здесь проверяется разбор вывода резервного подключения — то место, где
# незамеченная правка выносит в журнал устройство канала. Всё остальное в этом
# проекте проверяется руками, и это отдельный разговор; заводить обвес целиком
# ради одной правки было бы не по делу, а вот оставить эту без проверки нельзя.
add_executable(relaytrace_test EXCLUDE_FROM_ALL
        test/RelayTraceTest.cpp
        main/RelayTrace.cpp
)
target_include_directories(relaytrace_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(relaytrace_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

# Хранилище реквизитов: запечатка платформенная, поэтому набор линкует ту же
# реализацию, что и приложение, а не заглушку. Иначе проверка «на диске нет
# читаемых ключей» проверяла бы не то, что поедет людям.
add_executable(credentials_test EXCLUDE_FROM_ALL
        test/DeviceCredentialsTest.cpp
        main/DeviceCredentials.cpp
        main/RelayActivationParse.cpp
        ${SEALED_STORE_SOURCE}
)
target_include_directories(credentials_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(credentials_test PRIVATE Qt${QT_VERSION_MAJOR}::Core ${SEALED_STORE_LIBRARIES})

# Разбор ответов о компоненте резерва. Без сети: цена ошибки здесь не в отказе,
# а в неверном совете — см. шапку набора.
# Исключения из туннеля против собственных поддельных диапазонов. Две постоянные
# в двух файлах, обязанные сходиться; разошлись молча — см. шапку набора.
add_executable(tun_exclude_test EXCLUDE_FROM_ALL
        test/TunExcludeTest.cpp
)
target_link_libraries(tun_exclude_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
target_include_directories(tun_exclude_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Отчёт о туннеле против правила подмены стека в ядре. Правило продублировано
# на двух языках в двух файлах; разойтись они могут молча, и тогда отчёт врёт
# ровно там, где ему верят больше всего — см. шапку набора.
# Порядок правил маршрутизации: явное исключение против широкой категории.
# Перестановка этих строк ничего не ломает при сборке — она возвращает
# неисправимый блок, и виден он только тем, что что-то перестало открываться.
# Разбор «что случилось с программой»: чистый счёт по записям ядра, без окон и
# без сети. Потому и проверяется набором, а не ломанием собственной сети.

# Состояние портов машины: чистый разбор вывода системы, без запуска её самой.
add_executable(port_health_test EXCLUDE_FROM_ALL
        test/PortHealthTest.cpp
        main/PortHealth.cpp
)
target_link_libraries(port_health_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(port_health_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(program_trouble_test EXCLUDE_FROM_ALL
        test/ProgramTroubleTest.cpp
        main/ProgramTrouble.cpp
)
target_link_libraries(program_trouble_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(program_trouble_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(route_order_test EXCLUDE_FROM_ALL
        test/RouteOrderTest.cpp
)
target_link_libraries(route_order_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(route_order_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(tun_report_test EXCLUDE_FROM_ALL
        test/TunReportTest.cpp
)
target_link_libraries(tun_report_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(tun_report_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(relay_component_test EXCLUDE_FROM_ALL
        test/RelayComponentTest.cpp
        main/RelayComponentParse.cpp
)
target_link_libraries(relay_component_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(relay_component_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Сборка файла автонастройки прокси для macOS. Цель собирается и запускается на
# ЛЮБОЙ платформе намеренно: сам PacBuilder платформенного кода не содержит, а
# проверить его на маке из-под Windows нечем. Ошибка здесь не отказывает — она
# молча уводит часть трафика не туда, поэтому оставлять её без проверки нельзя.
add_executable(pac_test EXCLUDE_FROM_ALL
        test/PacBuilderTest.cpp
        sys/macos/PacBuilder.cpp
)
target_include_directories(pac_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pac_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)

# Снимок вёрстки диалога — чтобы на него можно было посмотреть, не запуская
# клиент. Линкует только Widgets и сгенерированный uic-заголовок.
# Заголовок берётся тот же, что собрал uic для приложения: два AUTOUIC на один
# .ui дерутся за каталог autogen, а второй копии вёрстки нам не нужно — смысл
# как раз в том, чтобы смотреть на ту же самую.
add_executable(relay_dialog_preview EXCLUDE_FROM_ALL test/RelayDialogPreview.cpp)
add_dependencies(relay_dialog_preview greenrhythm)
target_include_directories(relay_dialog_preview PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/greenrhythm_autogen/include)
target_link_libraries(relay_dialog_preview PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# Снимок панели «Зелёный Ритм». Панель длинная и собирается кодом, а не из .ui,
# поэтому увидеть её иначе как запуском клиента нельзя — а на машине владельца
# поднят рабочий туннель, и второй экземпляр ради вёрстки того не стоит.
# Снимок ГЛАВНОГО окна. До него правки внешнего вида шли вслепую: посмотреть на
# окно можно было только запустив клиент поверх рабочего туннеля владельца.
# Заголовок берётся тот же, что uic собрал для приложения, — второй AUTOUIC на
# один .ui дал бы две расходящиеся вёрстки.
add_executable(mainwindow_preview EXCLUDE_FROM_ALL
        test/MainWindowPreview.cpp
        ui/MainShell.cpp
        ui/ServerCardDelegate.cpp
        main/ConnectionRow.cpp
        main/AppFont.cpp
        # Ресурсы: без них значки в колонке возьмутся из пустоты, и снимок
        # покажет аккуратную вёрстку без единого значка — то есть соврёт.
        res/neko.qrc
)
set_property(TARGET mainwindow_preview PROPERTY AUTORCC ON)
set_property(TARGET mainwindow_preview PROPERTY AUTOMOC ON)
add_dependencies(mainwindow_preview greenrhythm)
target_include_directories(mainwindow_preview PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/greenrhythm_autogen/include)
target_link_libraries(mainwindow_preview PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# СТОРОЖ ВРЕМЕНИ ЖИЗНИ ВИДЖЕТОВ. Нужен заголовок, собранный uic для
# приложения, и оболочка — как у предпросмотра выше, по той же причине: второй
# AUTOUIC на один .ui дал бы две расходящиеся вёрстки.
# СТОРОЖ САМОЙ РАЗРУШИТЕЛЬНОЙ ФУНКЦИИ. Читает исходник и проверяет отношения:
# своё исключается во всех проходах, обещание окна совпадает со скриптом.
# Только Qt::Core — ни виджетов, ни системных вызовов.
add_executable(network_repair_test EXCLUDE_FROM_ALL test/NetworkRepairTest.cpp)
target_link_libraries(network_repair_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(network_repair_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(widget_lifetime_test EXCLUDE_FROM_ALL
        test/WidgetLifetimeTest.cpp
        ui/MainShell.cpp
        ui/ServerCardDelegate.cpp
        res/neko.qrc
)
set_property(TARGET widget_lifetime_test PROPERTY AUTORCC ON)
set_property(TARGET widget_lifetime_test PROPERTY AUTOMOC ON)
add_dependencies(widget_lifetime_test greenrhythm)
target_include_directories(widget_lifetime_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/greenrhythm_autogen/include)
target_link_libraries(widget_lifetime_test PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# План запуска winws — золотым набором. Аргументы строятся один раз и уезжают
# в кернел-драйвер: ошибка в порядке профилей или пустой список означают не
# «не сработало», а «дурим весь трафик машины». Набор чистый: ни сети, ни
# системы, ни winws — сравнивается ровно то, что вернула buildPlan.
# Помехи: приостановка чужого и возврат его как было. Единственное место, где
# мы выключаем ЧУЖУЮ работающую программу, — и цена ошибки не в нашем клиенте, а
# в рабочем VPN человека, который он не свяжет с нами. Проверять на живой машине
# нечем: каждая проверка означала бы выключить себе сеть.
# Подписи строки соединений. Таблица — то, куда смотрят, когда не работает;
# три строки со снимка владельца там читались как программы мимо VPN, а были
# нашим же туннелем.
add_executable(connection_row_test EXCLUDE_FROM_ALL
        test/ConnectionRowTest.cpp
        main/ConnectionRow.cpp
)
target_link_libraries(connection_row_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(connection_row_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(interference_test EXCLUDE_FROM_ALL
        test/InterferenceTest.cpp
        main/Interference.cpp
)
# Qt::Network — ради QHostAddress: вложенность префиксов считается разбором
# адреса, а не строк, иначе 172.32.0.0/16 «попал бы» в 172.16.0.0/12.
target_link_libraries(interference_test PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Network)
target_include_directories(interference_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(dpi_plan_test EXCLUDE_FROM_ALL
        test/DpiPlanTest.cpp
        dpi/DpiPlan.cpp
        dpi/DpiCatalog.cpp
)
target_link_libraries(dpi_plan_test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
target_include_directories(dpi_plan_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(greenrhythm_panel_preview EXCLUDE_FROM_ALL
        test/GreenRhythmPanelPreview.cpp
        ui/dialog_greenrhythm.cpp
        ui/dialog_whatbroke.cpp
        main/ProgramTrouble.cpp
        main/RunningPrograms.cpp
        # Ресурсы приложения целиком: иначе знак в шапке возьмётся из пустоты, и
        # снимок покажет аккуратную вёрстку с невидимой иконкой — то есть соврёт
        # ровно про то, что мы пришли проверять.
        res/neko.qrc
)
set_property(TARGET greenrhythm_panel_preview PROPERTY AUTORCC ON)
target_include_directories(greenrhythm_panel_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(greenrhythm_panel_preview PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
# Свой AUTOMOC: у панели есть Q_OBJECT и сигналы, без обработки moc'ом линковка
# не найдёт ни staticMetaObject, ни тела сигналов.
set_property(TARGET greenrhythm_panel_preview PROPERTY AUTOMOC ON)

# Снимок экрана выбора режима. Экран показывается только на macOS, а смотреть на
# него надо здесь — иначе вёрстка проверяется впервые у человека. Собирается на
# любой платформе, потому что и сам класс собирается везде.
add_executable(macos_mode_preview EXCLUDE_FROM_ALL
        test/MacosModePreview.cpp
        ui/dialog_macos_mode.cpp
)
# Свой AUTOMOC: у диалога есть Q_OBJECT, и без обработки moc'ом линковка не
# найдёт staticMetaObject. Слоя настроек здесь нет намеренно — именно ради
# этого хранение и вынесено из диалога.
set_property(TARGET macos_mode_preview PROPERTY AUTOMOC ON)
target_include_directories(macos_mode_preview PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_BINARY_DIR}/greenrhythm_autogen/include)
target_link_libraries(macos_mode_preview PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

# ЗАПУСК ПРОВЕРОК: один список, а не четыре.
#
# Наборов девять, и все они EXCLUDE_FROM_ALL — то есть `ninja` их не собирает, а
# `ctest` не запускал вовсе: ни enable_testing, ни add_test здесь не было.
# Список целей при этом был перепечатан руками в CI, в двух разных местах.
# Заведи кто-нибудь десятый набор и забудь про одну из тех строк — набор не
# выполнится ни разу, и никто об этом не узнает: непройденная проверка выглядит
# точно так же, как отсутствующая.
#
# Теперь список живёт здесь. `ninja build-tests && ctest --output-on-failure`
# собирает и гоняет всё; CI зовёт то же самое.
enable_testing()

set(GR_TEST_TARGETS
        relaytrace_test
        credentials_test
        relay_component_test
        pac_test
        tun_exclude_test
        tun_report_test
        route_order_test
        program_trouble_test
        port_health_test
        widget_lifetime_test
        network_repair_test
        dpi_plan_test
        interference_test
        connection_row_test
)

add_custom_target(build-tests DEPENDS ${GR_TEST_TARGETS})

# Рабочий каталог оставлен по умолчанию — каталогом сборки, и это не мелочь.
# Наборы, читающие файлы дерева (TunReportTest сверяет правило между Go и C++),
# сами пробуют префиксы "", "../", "../../" и оттуда всё находят. А вот
# pac_test пишет рядом с собой pac-sample.pac, который потом исполняет
# support/Test-Pac.js, и credentials_test пишет device.dat: запусти мы их из
# корня — они бы мусорили в исходниках.
# Наборам нужен Qt рядом. Запуская их руками, про это помнишь; ctest про это не
# знает, и без подсказки все девять падают с «библиотека не найдена» — то есть
# выглядят как сломанные, ничего не проверив.
set(GR_TEST_QT_BIN "")
if (WIN32)
    get_target_property(GR_QT_CORE_DLL Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION_RELEASE)
    if (NOT GR_QT_CORE_DLL)
        get_target_property(GR_QT_CORE_DLL Qt${QT_VERSION_MAJOR}::Core IMPORTED_LOCATION)
    endif ()
    if (GR_QT_CORE_DLL)
        get_filename_component(GR_TEST_QT_BIN "${GR_QT_CORE_DLL}" DIRECTORY)
    endif ()
endif ()

foreach (gr_test IN LISTS GR_TEST_TARGETS)
    add_test(NAME ${gr_test} COMMAND ${gr_test})
    if (GR_TEST_QT_BIN)
        if (CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
            # Дописывает к существующему PATH, ничего не затирая.
            set_tests_properties(${gr_test} PROPERTIES
                    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${GR_TEST_QT_BIN}")
        else ()
            # На старом CMake свойство ENVIRONMENT задаёт переменную целиком,
            # поэтому системный PATH приходится дописывать самим. Точка с запятой
            # для CMake — разделитель списка, отсюда экранирование.
            string(REPLACE ";" "\;" GR_SYS_PATH "$ENV{PATH}")
            set_tests_properties(${gr_test} PROPERTIES
                    ENVIRONMENT "PATH=${GR_TEST_QT_BIN}\;${GR_SYS_PATH}")
        endif ()
    endif ()
endforeach ()
