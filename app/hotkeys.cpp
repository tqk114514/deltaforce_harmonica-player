#include "hotkeys.h"

#include <QDebug>
#include <QWindow>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace harmonica::app {

Hotkeys::Hotkeys(QObject* parent) : QObject{parent} {
    // 键位和 README 里那张热键表一一对应
    bindings_ = {
        Binding{F9_HOTKEY_ID, VK_F9, QStringLiteral("F9")},
        Binding{F10_HOTKEY_ID, VK_F10, QStringLiteral("F10")},
    };
}

Hotkeys::~Hotkeys() { unregister(); }

bool Hotkeys::registerTo(QWindow* window) {
    if (window == nullptr) return false;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    hwnd_ = reinterpret_cast<unsigned long long>(hwnd);

    bool all = true;
    for (const Binding& binding : bindings_) {
        // id 用来在 WM_HOTKEY 里认是哪个键；修饰键掩码为 0 —— 要的就是裸 F9 / F10
        if (!::RegisterHotKey(hwnd, binding.id, 0, binding.vk)) {
            qWarning() << "全局热键注册失败（多半是被别的程序占了）:" << binding.name;
            all = false;
        }
    }
    return all;
}

void Hotkeys::unregister() {
    if (hwnd_ == 0) return;
    for (const Binding& binding : bindings_) {
        ::UnregisterHotKey(reinterpret_cast<HWND>(hwnd_), binding.id);
    }
    hwnd_ = 0;
}

bool Hotkeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if (eventType != "windows_generic_MSG" || message == nullptr) return false;

    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY
        || reinterpret_cast<unsigned long long>(msg->hwnd) != hwnd_) {
        return false;
    }
    for (const Binding& binding : bindings_) {
        if (binding.id == static_cast<int>(msg->wParam)) {
            emit activated(binding.name);
            if (result != nullptr) *result = 0;
            return true;
        }
    }
    return false;
}

}  // namespace harmonica::app
