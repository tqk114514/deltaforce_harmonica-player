# 用经典 AUTOMOC：测试类声明在 .cpp 里、末尾 #include "xxx.moc"
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

foreach(case IN ITEMS score config player preview input)
  add_executable(tst_${case} tst_${case}.cpp)
  target_link_libraries(tst_${case} PRIVATE harmonica::core Qt6::Test)
  # 测试是控制台程序：被建成 GUI 子系统就看不到失败原因了
  set_target_properties(tst_${case} PROPERTIES WIN32_EXECUTABLE FALSE)
  add_test(NAME ${case} COMMAND tst_${case})
  # Qt6Core.dll 得在 PATH 上才起得来（否则 0xc0000135）。从已找到的 Qt 反推目录，
  # 不把 D:/Qt 这种本机路径写进仓库。
  set_tests_properties(${case} PROPERTIES ENVIRONMENT "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}")
endforeach()

# tst_input 要拿 MOUSEEVENTF_* 常量核对映射方向
target_link_libraries(tst_input PRIVATE user32.lib)
