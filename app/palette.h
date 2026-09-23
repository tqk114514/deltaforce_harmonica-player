#pragma once

//! 一套颜色，两个地方共用：QML 界面读它，Win32 标题栏染色也读它。
//!
//! 只能有一份 —— 标题栏和窗口内容之间裂开一条看得见的缝，就是两边各写一遍的结果。

#include <QColor>

namespace harmonica::app {

inline const QColor WINDOW_BACKGROUND = QColor::fromRgb(0xFF16161B);
/// 输入框、列表这类「浮在底色上」的面，和图标背景色同一个值
inline const QColor SURFACE = QColor::fromRgb(0xFF1F1F24);
inline const QColor WINDOW_TEXT = QColor::fromRgb(0xFFEDEDF1);
inline const QColor BORDER = QColor::fromRgb(0xFF292932);
inline const QColor ACCENT = QColor::fromRgb(0xFFE8A33D);
inline const QColor MUTED_TEXT = QColor::fromRgb(0xFF8A8A99);

}  // namespace harmonica::app
