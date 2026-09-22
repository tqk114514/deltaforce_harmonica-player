#include "input.h"

#include <atomic>
#include <span>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace harmonica::input {
namespace {

std::atomic<std::uint64_t> sentOk{0};
std::atomic<std::uint64_t> sentFailed{0};

bool push(std::span<const INPUT> inputs) {
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()),
                               const_cast<INPUT*>(inputs.data()), static_cast<int>(sizeof(INPUT)));
    if (sent == inputs.size()) {
        sentOk.fetch_add(sent, std::memory_order_relaxed);
        return true;
    }
    // 常见原因：目标进程以管理员运行而本程序不是，Windows 会静默丢弃
    sentFailed.fetch_add(inputs.size(), std::memory_order_relaxed);
    return false;
}

/// 用扫描码模式发送键盘事件。
/// 部分游戏只认硬件扫描码，不认虚拟键码，所以 wVk 留空、只给 wScan。
bool sendKey(std::uint16_t vk, bool up) {
    const auto scan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    DWORD flags = KEYEVENTF_SCANCODE;
    if (up) flags |= KEYEVENTF_KEYUP;

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = scan;
    input.ki.dwFlags = flags;
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;
    return push(std::span<const INPUT>{&input, 1});
}

}  // namespace

bool keyDown(std::uint16_t vk) { return sendKey(vk, false); }
bool keyUp(std::uint16_t vk) { return sendKey(vk, true); }

std::uint32_t mouseFlags(MouseButton button, MouseAction action) {
    switch (button) {
        case MouseButton::Left:
            return action == MouseAction::Down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case MouseButton::Middle:
            return action == MouseAction::Down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        case MouseButton::Right:
            return action == MouseAction::Down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    }
    return MOUSEEVENTF_LEFTUP;
}

bool mouse(MouseButton button, MouseAction action) {
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dx = 0;
    input.mi.dy = 0;
    input.mi.mouseData = 0;
    input.mi.dwFlags = mouseFlags(button, action);
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;
    return push(std::span<const INPUT>{&input, 1});
}

bool mouseDown(MouseButton button) { return mouse(button, MouseAction::Down); }
bool mouseUp(MouseButton button) { return mouse(button, MouseAction::Up); }

std::pair<std::uint64_t, std::uint64_t> stats() {
    return {sentOk.load(std::memory_order_relaxed), sentFailed.load(std::memory_order_relaxed)};
}

void resetStats() {
    sentOk.store(0, std::memory_order_relaxed);
    sentFailed.store(0, std::memory_order_relaxed);
}

void releaseAll(const std::vector<std::uint16_t>& keys) {
    for (const std::uint16_t vk : keys) keyUp(vk);
    for (const MouseButton button : {MouseButton::Left, MouseButton::Middle, MouseButton::Right}) {
        mouseUp(button);
    }
}

}  // namespace harmonica::input
