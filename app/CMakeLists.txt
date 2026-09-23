find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2 Widgets Network Multimedia)

# Backend 是 Q_OBJECT，QML 类型注册还要从它抽 metatype —— 没有 AUTOMOC 都不成立
set(CMAKE_AUTOMOC ON)

# 外壳拆成静态库：路径约定和 Win32 属性这两块必须能被测试盯住，
# 而测试没法从「一个可执行文件」里链接代码。
add_library(harmonica_shell STATIC
    paths.cpp
    library.cpp
    playersession.cpp
    scoredoc.cpp
    community.cpp
    audio.cpp
    hotkeys.cpp
    win32.cpp
    backend.cpp
)
target_include_directories(harmonica_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(harmonica_shell PUBLIC
    harmonica::core
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Multimedia
    user32.lib
    dwmapi.lib
    shell32.lib
)
target_compile_options(harmonica_shell PRIVATE /utf-8 /W4 /permissive-)

qt_add_executable(harmonica-player WIN32
    main.cpp
)

# Debug 版带控制台：QML 的告警和错误只有 stderr 这一条通道，
# 而 GUI 子系统程序在开发时收不到它（Release 不带，免得双击图标时闪出一个黑窗）。
set_target_properties(harmonica-player PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)

qt_add_qml_module(harmonica-player
    URI Harmonica
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Overlay.qml
        qml/Sidebar.qml
        qml/LibraryPage.qml
        qml/SettingsPage.qml
        qml/PagePlaceholder.qml
)

# 窗口图标和托盘图标共用这一个 .ico（由 tools/make_icons.py 生成，编进资源）
qt_add_resources(harmonica-player "app_resources"
    PREFIX "/"
    FILES icons/icon.ico
)

target_link_libraries(harmonica-player PRIVATE
    harmonica_shell
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Widgets
)

target_compile_definitions(harmonica-player PRIVATE HARMONICA_VERSION="${PROJECT_VERSION}")
target_compile_options(harmonica-player PRIVATE /utf-8 /W4 /permissive-)

# 提权声明：Release 里要 requireAdministrator，Debug 故意保持 asInvoker ——
# 开发期每次启动都弹 UAC 太折磨人（旧版 README 就抱怨过），而冒烟测试只能在 Debug 跑。
#
# 不靠链接器选项：CMake 会在链接命令**末尾**补一个 /MANIFEST:NO，而 link.exe 是后写的生效，
# 结果就是清单根本没嵌进 exe（提权只存在于旁边那个 .exe.manifest 文件里，单独发 exe 就失效）。
# 所以正常构建完再用 mt.exe 把清单写进 exe 的 #1 资源 —— 有且只有一份，可实测。
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(HARMONICA_UAC_LEVEL "requireAdministrator")
else()
  set(HARMONICA_UAC_LEVEL "asInvoker")
endif()
string(REPLACE "." ".0" HARMONICA_MANIFEST_VERSION "${PROJECT_VERSION}.0")
configure_file(app.manifest ${CMAKE_CURRENT_BINARY_DIR}/harmonica-player.manifest @ONLY)

find_program(HARMONICA_MT NAMES mt.exe mt
             HINTS "$ENV{SDK_PATH}bin" ENV PATH)
if(NOT HARMONICA_MT)
  message(FATAL_ERROR "找不到 Windows SDK 的 mt.exe（要在开发者命令行里构建，或传 -DHARMONICA_MT=<路径>）")
endif()

# 注意不要加 VERBATIM：`\;` 就是靠「非 VERBATIM」才会被当成一个字面分号传给 mt.exe，
# 加了 VERBATIM 反而把 `;#1` 拆成两个参数（mt 报 c1010011）。
# mt.exe 的参数形态是 `文件;#资源号`，而这个分号和井号在 CMake / Ninja / cmd 三层里
# 都会被吃掉一点（试过 `\;`、引号、VERBATIM，渲染出来不是少分号就是少 `#1`）。
# 所以配置期直接写出一个一次性的批处理，路径全是已知的，不再和引号搏斗。
set(HARMONICA_MANIFEST_FILE ${CMAKE_CURRENT_BINARY_DIR}/harmonica-player.manifest)
set(HARMONICA_EXE ${CMAKE_CURRENT_BINARY_DIR}/harmonica-player.exe)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/embed-manifest.cmd
     "@echo off\r\n\"${HARMONICA_MT}\" -manifest \"${HARMONICA_MANIFEST_FILE}\" -outputresource:\"${HARMONICA_EXE};#1\"\r\nexit /b %ERRORLEVEL%\r\n")

add_custom_command(TARGET harmonica-player POST_BUILD
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/embed-manifest.cmd
    COMMENT "把应用清单嵌进 exe（${HARMONICA_UAC_LEVEL}）")

if(HARMONICA_BUILD_TESTS)
  add_subdirectory(tests)
endif()
