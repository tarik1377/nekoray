cmake_minimum_required(VERSION 3.22)
project(GreenRhythmSidebarReview LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)
get_filename_component(GR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
enable_testing()
foreach(test IN ITEMS SidebarActions PowerButton PowerState SettingsPage)
    string(REGEX REPLACE "([a-z])([A-Z])" "\\1_\\2" target "${test}")
    string(TOLOWER "${target}_test" target)
    add_executable(${target}
        "${GR_SOURCE_DIR}/test/${test}Test.cpp"
        "${GR_SOURCE_DIR}/ui/MainShell.cpp"
        "${GR_SOURCE_DIR}/ui/Icons.cpp"
        "${GR_SOURCE_DIR}/ui/ServerCardDelegate.cpp"
        "${GR_SOURCE_DIR}/main/AppFont.cpp"
        "${GR_SOURCE_DIR}/res/neko.qrc")
    target_include_directories(${target} PRIVATE "${GR_SOURCE_DIR}")
    target_link_libraries(${target} PRIVATE Qt6::Widgets Qt6::Svg)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES WORKING_DIRECTORY "${GR_SOURCE_DIR}"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endforeach()
