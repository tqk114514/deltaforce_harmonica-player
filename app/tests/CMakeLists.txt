set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

foreach(case IN ITEMS paths win32 library session hotkeys settings scoredoc community editor)
  add_executable(tst_shell_${case} tst_${case}.cpp)
  target_link_libraries(tst_shell_${case} PRIVATE harmonica_shell Qt6::Test)
  # REQUIRE_OK / REQUIRE_MSG 这两个宏在 core/tests/testhelpers.h 里，
  # 两套测试共用一份（消息表达式会被无条件求值那个坑，两边都会踩）
  target_include_directories(tst_shell_${case} PRIVATE ${CMAKE_SOURCE_DIR}/core/tests)
  set_target_properties(tst_shell_${case} PROPERTIES WIN32_EXECUTABLE FALSE)
  add_test(NAME shell_${case} COMMAND tst_shell_${case})
  set_tests_properties(shell_${case} PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
endforeach()
