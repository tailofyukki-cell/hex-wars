cmake_minimum_required(VERSION 3.16)
project(hexwars C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- SDL2（third_party のプレビルド VC 版） ----
set(SDL2_ROOT "${CMAKE_SOURCE_DIR}/third_party/SDL2-2.32.10")
set(TTF_ROOT  "${CMAKE_SOURCE_DIR}/third_party/SDL2_ttf-2.24.0")
set(MIX_ROOT  "${CMAKE_SOURCE_DIR}/third_party/SDL2_mixer-2.8.1")
set(IMG_ROOT  "${CMAKE_SOURCE_DIR}/third_party/SDL2_image-2.8.12")

if(MSVC)
    # Debug は警告をエラー扱い（仕様書 2.2）
    add_compile_options(/W4 /utf-8 "$<$<CONFIG:Debug>:/WX>")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra "$<$<CONFIG:Debug>:-Werror>")
endif()

# core / data 層の SDL 依存を検出（仕様書 13章: レイヤ違反チェック）
file(GLOB LAYER_CHECK_FILES
    "${CMAKE_SOURCE_DIR}/src/core/*.c" "${CMAKE_SOURCE_DIR}/src/core/*.h"
    "${CMAKE_SOURCE_DIR}/src/data/*.c" "${CMAKE_SOURCE_DIR}/src/data/*.h")
foreach(_f ${LAYER_CHECK_FILES})
    file(READ "${_f}" _content)
    if(_content MATCHES "#include[ \t]*[<\"]SDL")
        message(FATAL_ERROR "レイヤ違反: core/data 層が SDL を include しています: ${_f}")
    endif()
endforeach()

# ---- core / data（SDL非依存） ----
set(CORE_SOURCES
    src/core/hex.c
    src/core/rng.c
    src/core/game.c
    src/core/battle.c
    src/core/rules.c
    src/core/path.c
    src/core/ai.c
    src/core/campaign.c
    src/core/save.c
    src/data/parser.c
)

# ---- 本体 ----
add_executable(hexwars
    ${CORE_SOURCES}
    src/main.c
    src/ui/assets.c
    src/ui/render.c
    src/ui/screens.c
    src/ui/screen_battle.c
    src/ui/sound.c
    src/ui/text.c
    src/ui/sprites.c
    src/ui/anim.c
)
target_include_directories(hexwars PRIVATE
    "${SDL2_ROOT}/include"
    "${TTF_ROOT}/include"
    "${MIX_ROOT}/include"
    "${IMG_ROOT}/include"
)
# GUIアプリとしてビルドし、起動時のコンソール窓を出さない
# （Debug構成のみコンソールを残して SDL_Log を見られるようにする）
set_target_properties(hexwars PROPERTIES
    WIN32_EXECUTABLE "$<NOT:$<CONFIG:Debug>>")
if(WIN32)
    target_link_libraries(hexwars
        "${SDL2_ROOT}/lib/x64/SDL2main.lib"
        "${SDL2_ROOT}/lib/x64/SDL2.lib"
        "${TTF_ROOT}/lib/x64/SDL2_ttf.lib"
        "${MIX_ROOT}/lib/x64/SDL2_mixer.lib"
        "${IMG_ROOT}/lib/x64/SDL2_image.lib"
    )
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(hexwars SDL2::SDL2 SDL2_ttf SDL2_mixer SDL2_image m)
endif()

# DLL・データ・アセットを実行ファイル横へコピー
if(WIN32)
    add_custom_command(TARGET hexwars POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL2_ROOT}/lib/x64/SDL2.dll" "$<TARGET_FILE_DIR:hexwars>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TTF_ROOT}/lib/x64/SDL2_ttf.dll" "$<TARGET_FILE_DIR:hexwars>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MIX_ROOT}/lib/x64/SDL2_mixer.dll" "$<TARGET_FILE_DIR:hexwars>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${IMG_ROOT}/lib/x64/SDL2_image.dll" "$<TARGET_FILE_DIR:hexwars>"
    )
endif()
add_custom_command(TARGET hexwars POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:hexwars>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:hexwars>/assets"
)

# ---- 単体テスト（SDL不要） ----
enable_testing()
add_executable(test_core ${CORE_SOURCES} tests/test_core.c)
add_test(NAME core
    COMMAND test_core
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
add_executable(sim_ai ${CORE_SOURCES} tests/sim_ai.c)
add_test(NAME ai_simulation
    COMMAND sim_ai
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
