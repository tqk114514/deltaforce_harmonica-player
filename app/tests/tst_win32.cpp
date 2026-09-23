#include <QWindow>
#include <QtTest>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "win32.h"

using harmonica::app::win32::makeNonActivating;
using harmonica::app::win32::tintCaption;

namespace {

LONG_PTR extraStyle(QWindow* window) {
    return ::GetWindowLongPtrW(reinterpret_cast<HWND>(window->winId()), GWL_EXSTYLE);
}

}  // namespace

/// 这些属性必须在悬浮窗**被显示出来之前**打好，所以是 C++ 侧的启动步骤，
/// 这里验的是「打上去之后确实读得回来」。
class TestShellWin32 : public QObject {
    Q_OBJECT

private slots:
    void nonActivatingSetsBothBits();
    void tintingDoesNotTouchActivation();
};

void TestShellWin32::nonActivatingSetsBothBits() {
    QWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    // winId() 会造出原生窗口（不显示），后面的调用才有 HWND 可用
    QVERIFY(window.winId() != 0);

    QVERIFY((extraStyle(&window) & WS_EX_NOACTIVATE) == 0);
    makeNonActivating(&window);

    const LONG_PTR after = extraStyle(&window);
    QVERIFY((after & WS_EX_NOACTIVATE) != 0);
    // 顺带从任务栏 / Alt+Tab 里摘掉：它本来就不该被当成一个正常的窗口
    QVERIFY((after & WS_EX_TOOLWINDOW) != 0);
    // 已经有的位不能被抹掉
    QVERIFY((after & WS_EX_TOPMOST) == (extraStyle(&window) & WS_EX_TOPMOST));
}

/// 染色只改颜色：深色模式那一条决定标题栏三个按钮的画法，漏了就把浅色图标画在深色底上
void TestShellWin32::tintingDoesNotTouchActivation() {
    QWindow window;
    window.setFlags(Qt::Tool | Qt::FramelessWindowHint);
    QVERIFY(window.winId() != 0);
    const LONG_PTR before = extraStyle(&window);

    tintCaption(&window);
    QCOMPARE(extraStyle(&window), before);

    // 空指针是「窗口还没建好」的情况，静默返回而不是崩
    tintCaption(nullptr);
    makeNonActivating(nullptr);
}

QTEST_MAIN(TestShellWin32)
#include "tst_win32.moc"
