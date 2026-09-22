#pragma once

//! Windows 输入层：SendInput 封装。
//!
//! 只用系统输入 API，不读游戏内存、不注入进程。

#include <cstdint>
#include <utility>
#include <vector>

#include "config.h"

namespace harmonica::input {

/// 鼠标键的动作方向。用枚举而不是 bool —— 曾经传反过一次，
/// 结果把「按下」发成了「抬起」，导致鼠标键卡住、整个系统点不动。
enum class MouseAction {
    Down,
    Up,
};

bool keyDown(std::uint16_t vk);
bool keyUp(std::uint16_t vk);

bool mouseDown(MouseButton button);
bool mouseUp(MouseButton button);

/// 鼠标键 -> SendInput 的 `dwFlags` 数值（`MOUSEEVENTF_*`）。
/// 抽成纯函数是为了能被测试盯住，所以返回整数而不把 windows.h 漏进头文件。
std::uint32_t mouseFlags(MouseButton button, MouseAction action);

bool mouse(MouseButton button, MouseAction action);

/// 发送统计（成功数，被系统丢弃数）。SendInput 被拦截时是静默失败的，必须自己数。
std::pair<std::uint64_t, std::uint64_t> stats();
void resetStats();

/// 释放所有可能仍处于按下状态的键与鼠标键。停止、退出的每条路径都要调。
void releaseAll(const std::vector<std::uint16_t>& keys);

}  // namespace harmonica::input
