#pragma once

//! 测试用的小工具。
//!
//! Qt6 的 `QVERIFY2` 把消息表达式**无条件求值**（两个分支都要求），所以
//! 「先判 has_value、消息里再取 error()」这种写法会在成功路径上调用 error() ——
//! MSVC 的 debug 运行时对「有值还取 error」弹断言框，进程就那么挂在那儿，
//! 一行输出都没有。`qPrintable(临时 QString)` 还有个悬垂问题：临时量在
//! 语句结束就析构，QTest 拿到的是野指针。
//!
//! 所以：判断 expected 一律用 `REQUIRE_OK`；要带动态消息就先把 QString 存进局部变量。

#include <QString>

/// expected 有值就继续；没值就把原因报成失败并退出当前测试函数
#define REQUIRE_OK(expr)                                            \
    do {                                                            \
        const auto& checked_ = (expr);                              \
        if (!checked_.has_value()) QFAIL(qPrintable(checked_.error())); \
    } while (false)

/// 带动态消息的 QVERIFY。消息先落进一个局部 QString ——
/// `qPrintable(QStringLiteral(...).arg(...))` 那种写法里临时量当场就析构了。
#define REQUIRE_MSG(condition, message)          \
    do {                                         \
        const QString message_ = (message);      \
        QVERIFY2(condition, qPrintable(message_)); \
    } while (false)
