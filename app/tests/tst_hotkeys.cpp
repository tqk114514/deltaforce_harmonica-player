#include <QSignalSpy>
#include <QWindow>
#include <QtTest>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hotkeys.h"

using harmonica::app::F10_HOTKEY_ID;
using harmonica::app::F9_HOTKEY_ID;
using harmonica::app::Hotkeys;

/// 热键的注册结果不能拿来断言 —— 这台机器上 F9/F10 可能已经被别的程序占了，
/// 那是环境问题不是这里的 bug。所以测的是「收到 WM_HOTKEY 之后分发对不对」：
/// 自己往窗口投一条，看信号有没有按名字发出去。
class TestHotkeys : public QObject {
    Q_OBJECT

private slots:
    void hotkeyMessageMapsToItsName();
    void otherMessagesAreIgnored();
};

void TestHotkeys::hotkeyMessageMapsToItsName() {
    QWindow window;
    QVERIFY(window.winId() != 0);

    Hotkeys hotkeys;
    QGuiApplication::instance()->installNativeEventFilter(&hotkeys);
    hotkeys.registerTo(&window);

    QSignalSpy activated{&hotkeys, &Hotkeys::activated};
    ::PostMessageW(reinterpret_cast<HWND>(window.winId()), WM_HOTKEY, F9_HOTKEY_ID, 0);
    QVERIFY(activated.wait(2000));
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated[0][0].toString(), QStringLiteral("F9"));

    ::PostMessageW(reinterpret_cast<HWND>(window.winId()), WM_HOTKEY, F10_HOTKEY_ID, 0);
    QVERIFY(activated.wait(2000));
    QCOMPARE(activated.count(), 2);
    QCOMPARE(activated[1][0].toString(), QStringLiteral("F10"));
}

void TestHotkeys::otherMessagesAreIgnored() {
    QWindow window;
    QVERIFY(window.winId() != 0);

    Hotkeys hotkeys;
    QGuiApplication::instance()->installNativeEventFilter(&hotkeys);
    hotkeys.registerTo(&window);

    QSignalSpy activated{&hotkeys, &Hotkeys::activated};
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());
    ::PostMessageW(hwnd, WM_USER + 42, 0, 0);
    // 别的程序注册的快捷键（id 不是我们的）也不能被当成按了 F9
    ::PostMessageW(hwnd, WM_HOTKEY, 0x9999, 0);
    QTest::qWait(200);
    QCOMPARE(activated.count(), 0);
}

QTEST_MAIN(TestHotkeys)
#include "tst_hotkeys.moc"
