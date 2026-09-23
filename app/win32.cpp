#include "win32.h"

#include <QVector>
#include <QWindow>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>

#include "palette.h"

namespace harmonica::app::win32 {
namespace {

// Windows 11（build 22000）才有的三个 DWM 属性；不认识它们的系统会返回失败，忽略即可
constexpr DWORD DARK_MODE_ATTRIBUTE = 20;
constexpr DWORD BORDER_COLOR_ATTRIBUTE = 34;
constexpr DWORD CAPTION_COLOR_ATTRIBUTE = 35;
constexpr DWORD TEXT_COLOR_ATTRIBUTE = 36;

/// CSS 的 `#RRGGBB` 是 RGB 顺序，而 `COLORREF` 是 `0x00BBGGRR` —— 正好反着。
/// 自己拼这三个字节，不用 `RGB()` 宏（那个在 wingdi.h 里，和 lean_and_mean 打架）。
COLORREF toColorRef(const QColor& color) {
    return static_cast<COLORREF>(color.red() | (color.green() << 8) | (color.blue() << 16));
}

QVector<WORD> toWide(const QString& text) {
    QVector<WORD> out;
    out.reserve(text.size() + 1);
    for (const QChar c : text) out.append(static_cast<WORD>(c.unicode()));
    out.append(0);
    return out;
}

HWND hwndOf(QWindow* window) {
    if (window == nullptr) return nullptr;
    return reinterpret_cast<HWND>(window->winId());
}

void setAttribute(HWND hwnd, DWORD attribute, DWORD value) {
    ::DwmSetWindowAttribute(hwnd, attribute, &value, sizeof(value));
}

/// 把窗口叫到最前面并给它焦点。
///
/// **没有哪一步单独靠得住**，所以连着做三步：
/// 1. `ShowWindow(SW_RESTORE)` —— 最小化的还原、隐藏的显示。
/// 2. 先置顶再取消置顶 —— Z 序不受「防抢焦点」规则管辖，这一步保证窗口一定露出来。
/// 3. 争前台。`SetForegroundWindow` 在调用者不是前台进程时会被**静默拒绝**，
///    这正是从托盘菜单过来的处境：先 `AttachThreadInput` 共享前台身份，
///    万一还不成再用 `SwitchToThisWindow` 兜一次。
/// 每步都忽略返回值：尽力而为，失败了也不该炸掉程序。
bool forceForeground(HWND hwnd) {
    ::ShowWindow(hwnd, SW_RESTORE);

    constexpr UINT swpNoActivate = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW;
    ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, swpNoActivate);
    ::SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    const DWORD foregroundThread = ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr);
    const DWORD thisThread = ::GetCurrentThreadId();
    const bool attached = foregroundThread != 0 && foregroundThread != thisThread
                          && ::AttachThreadInput(thisThread, foregroundThread, TRUE);

    ::SetForegroundWindow(hwnd);
    ::BringWindowToTop(hwnd);
    if (::GetForegroundWindow() != hwnd) ::SwitchToThisWindow(hwnd, TRUE);
    const bool focused = ::GetForegroundWindow() == hwnd;

    if (attached) ::AttachThreadInput(thisThread, foregroundThread, FALSE);
    return focused;
}

}  // namespace

bool takeSingleInstanceLock() {
    static HANDLE mutex = nullptr;
    if (mutex != nullptr) return true;  // 本进程已经拿着锁了

    const auto name = toWide(QString::fromLatin1(INSTANCE_MUTEX_NAME));
    // 句柄**故意不关**：它活着，那个互斥体就存在；进程一结束系统就收回。
    mutex = ::CreateMutexW(nullptr, TRUE, reinterpret_cast<LPCWSTR>(name.data()));
    // 必须紧接着读 GetLastError：别的 API 调用会把它覆盖掉
    if (mutex == nullptr) return true;  // 连锁都建不出来就别挡着用户启动
    return ::GetLastError() != ERROR_ALREADY_EXISTS;
}

bool focusRunningInstance(const QString& title) {
    const auto wide = toWide(title);
    const HWND hwnd = ::FindWindowW(nullptr, reinterpret_cast<LPCWSTR>(wide.data()));
    if (hwnd == nullptr) return false;
    return forceForeground(hwnd);
}

bool isElevated() {
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = ::GetTokenInformation(token, TokenElevation, &elevation,
                                          sizeof(elevation), &size);
    ::CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

void tintCaption(QWindow* window) {
    HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return;
    // 深色模式不只是「底色变深」：它还决定标题栏上那三个按钮的画法。
    // 漏掉这一条，浅色的按钮图标会画在染过的深色底上，直接看不见。
    setAttribute(hwnd, DARK_MODE_ATTRIBUTE, TRUE);
    setAttribute(hwnd, CAPTION_COLOR_ATTRIBUTE, toColorRef(WINDOW_BACKGROUND));
    setAttribute(hwnd, TEXT_COLOR_ATTRIBUTE, toColorRef(WINDOW_TEXT));
    setAttribute(hwnd, BORDER_COLOR_ATTRIBUTE, toColorRef(BORDER));
}

void makeNonActivating(QWindow* window) {
    HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return;
    const LONG_PTR current = ::GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ::SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                        current | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
}

void pinTopmostWithoutActivating(QWindow* window) {
    HWND hwnd = hwndOf(window);
    if (hwnd == nullptr) return;
    // SWP_NOACTIVATE 明确要求系统只改 Z 序、不动焦点
    ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool bringToForeground(QWindow* window) {
    const HWND hwnd = hwndOf(window);
    return hwnd != nullptr && forceForeground(hwnd);
}

bool openPath(const QString& path) {
    const auto operation = toWide(QStringLiteral("open"));
    const auto target = toWide(path);
    const HINSTANCE result = ::ShellExecuteW(nullptr, reinterpret_cast<LPCWSTR>(operation.data()),
                                            reinterpret_cast<LPCWSTR>(target.data()), nullptr,
                                            nullptr, SW_SHOWNORMAL);
    // ShellExecuteW 返回值 > 32 表示成功
    return reinterpret_cast<INT_PTR>(result) > 32;
}

}  // namespace harmonica::app::win32
