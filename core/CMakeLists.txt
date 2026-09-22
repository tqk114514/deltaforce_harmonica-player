add_library(harmonica_core STATIC
    score.cpp
    config.cpp
    player.cpp
    preview.cpp
    input.cpp
)
add_library(harmonica::core ALIAS harmonica_core)

target_include_directories(harmonica_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# SendInput / MapVirtualKeyW 在 user32；界面层通过 core 间接拿到它，自己不用链
target_link_libraries(harmonica_core PUBLIC Qt6::Core PRIVATE user32.lib)

# 源码是 UTF-8，中文字面量（错误提示、校准组名）要 /utf-8 才不会按本地代码页解读
target_compile_options(harmonica_core PRIVATE /utf-8 /W4 /permissive-)

if(HARMONICA_BUILD_TESTS)
  add_subdirectory(tests)
endif()
