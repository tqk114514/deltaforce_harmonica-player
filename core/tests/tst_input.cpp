#include <QtTest>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "input.h"

using namespace harmonica;
using harmonica::input::MouseAction;
using harmonica::MouseButton;

namespace {
constexpr std::uint32_t leftDown = MOUSEEVENTF_LEFTDOWN;
constexpr std::uint32_t leftUp = MOUSEEVENTF_LEFTUP;
constexpr std::uint32_t middleDown = MOUSEEVENTF_MIDDLEDOWN;
constexpr std::uint32_t middleUp = MOUSEEVENTF_MIDDLEUP;
constexpr std::uint32_t rightDown = MOUSEEVENTF_RIGHTDOWN;
constexpr std::uint32_t rightUp = MOUSEEVENTF_RIGHTUP;
}  // namespace

class TestInput : public QObject {
    Q_OBJECT

private slots:
    /// 曾经把 Down/Up 传反，导致「按下」发成「抬起」、鼠标键卡死。这个测试盯着映射方向。
    void mouseFlagsAreNotSwapped();
    /// 三个键的 down/up 各不相等，且不许串到别的键上
    void mouseFlagsDistinguishButtons();
};

void TestInput::mouseFlagsAreNotSwapped() {
    QCOMPARE(input::mouseFlags(MouseButton::Left, MouseAction::Down), leftDown);
    QCOMPARE(input::mouseFlags(MouseButton::Left, MouseAction::Up), leftUp);
    QCOMPARE(input::mouseFlags(MouseButton::Right, MouseAction::Down), rightDown);
    QCOMPARE(input::mouseFlags(MouseButton::Right, MouseAction::Up), rightUp);
    QCOMPARE(input::mouseFlags(MouseButton::Middle, MouseAction::Down), middleDown);
    QCOMPARE(input::mouseFlags(MouseButton::Middle, MouseAction::Up), middleUp);

    // DOWN 和 UP 必须是不同的标志，且 DOWN 不该等于任何 UP
    QVERIFY(input::mouseFlags(MouseButton::Left, MouseAction::Down)
            != input::mouseFlags(MouseButton::Left, MouseAction::Up));
    QVERIFY(input::mouseFlags(MouseButton::Left, MouseAction::Down)
            != input::mouseFlags(MouseButton::Right, MouseAction::Up));
}

void TestInput::mouseFlagsDistinguishButtons() {
    const std::uint32_t all[] = {
        input::mouseFlags(MouseButton::Left, MouseAction::Down),
        input::mouseFlags(MouseButton::Left, MouseAction::Up),
        input::mouseFlags(MouseButton::Middle, MouseAction::Down),
        input::mouseFlags(MouseButton::Middle, MouseAction::Up),
        input::mouseFlags(MouseButton::Right, MouseAction::Down),
        input::mouseFlags(MouseButton::Right, MouseAction::Up),
    };
    for (std::size_t i = 0; i < 6; ++i) {
        QVERIFY(all[i] != 0);
        for (std::size_t j = i + 1; j < 6; ++j) QVERIFY(all[i] != all[j]);
    }
}

QTEST_GUILESS_MAIN(TestInput)
#include "tst_input.moc"
