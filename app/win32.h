#pragma once

//! 所有直接操作 Win32 的地方。
//!
//! 悬浮窗不抢焦点、置顶不激活、标题栏染色、判断有没有提权 —— 这些在 Qt 的
//! 跨平台 API 里表达不出来，只能落到 Win32 上。全部集中在这个文件，
//! 换平台时只需要重写这里。
//!
//! 这里所有调用都对「系统太老」保持沉默：属性不认识就返回失败，忽略即可。

#include <QString>

class QWindow;

namespace harmonica::app::win32 {

/// 单实例锁的互斥体名。`Local\` 前缀 = 只在当前登录会话里唯一。
inline constexpr const char* INSTANCE_MUTEX_NAME = "Local\\com.deltaforce.harmonica-player";

/// 抢「同一时间只跑一个实例」的锁。抢到了返回 true。
/// 用命名互斥体而不是锁文件：进程被杀掉时系统会自动释放，不留要人清理的残留。
bool takeSingleInstanceLock();

/// 找到另一个实例的主窗口并叫到最前。按窗口标题找 —— 标题定死在 Main.qml 里。
bool focusRunningInstance(const QString& title);

/// 当前进程是否以管理员身份运行。游戏若是管理员，这个为 false 时按键一个都进不去。
bool isElevated();

/// 把系统原生标题栏染成应用底色（只用 DWM 改颜色，不自己画标题栏）。
/// 那三个属性是 Windows 11 build 22000 才有的，Win10 上会失败 —— 忽略即可。
void tintCaption(QWindow* window);

/// 让窗口不接受激活：点它、切到它，焦点都留在原来的窗口（也就是游戏）。
/// 同时把它从任务栏 / Alt+Tab 里摘掉。
void makeNonActivating(QWindow* window);

/// 置顶，但不激活自己。直接调 `raise()` 有激活窗口的风险，
/// 而这里一旦激活就把游戏的输入通道掐断了。
void pinTopmostWithoutActivating(QWindow* window);

/// 把最小化/后台的窗口叫到最前并给它焦点。返回是否真的拿到了前台。
bool bringToForeground(QWindow* window);

/// 交给系统外壳打开一个路径（目录会开资源管理器窗口）。
bool openPath(const QString& path);

}  // namespace harmonica::app::win32
