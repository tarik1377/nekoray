cmake_minimum_required(VERSION 3.22)
project(GreenRhythmGameProgramsTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
get_filename_component(GR_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
enable_testing()
add_executable(game_programs_test "${GR_SOURCE_DIR}/test/GameProgramsTest.cpp" "${GR_SOURCE_DIR}/ui/GameProgramsDialog.cpp")
target_include_directories(game_programs_test PRIVATE "${GR_SOURCE_DIR}")
target_link_libraries(game_programs_test PRIVATE Qt6::Widgets)
add_executable(game_voice_routing_test "${GR_SOURCE_DIR}/test/GameVoiceRoutingTest.cpp")
target_include_directories(game_voice_routing_test PRIVATE "${GR_SOURCE_DIR}")
target_link_libraries(game_voice_routing_test PRIVATE Qt6::Core)
target_compile_definitions(game_voice_routing_test PRIVATE
    GR_GAME_VOICE_FIXTURE="${GR_SOURCE_DIR}/go/cmd/nekobox_core/testdata/game_voice_rules.json"
    GR_GAME_VOICE_SNIFF_FIXTURE="${GR_SOURCE_DIR}/go/cmd/nekobox_core/testdata/game_voice_sniff.json")
foreach(target IN ITEMS game_programs_test game_voice_routing_test)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endforeach()
