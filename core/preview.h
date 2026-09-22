#pragma once

//! 试听用的音符列表。
//!
//! 这一层**不属于演奏路径**：演奏只关心「按哪个键、什么时候按」，从来不看音高频率。
//! 这里把谱面折算成「从曲首算起的时间 + 半音数」，给界面合成声音用。
//!
//! 时值复用 [`Song::msPerTick`]，和 [`buildActions`] 是同一套算法，所以试听听起来的
//! 节奏和真吹的一致。只有一处不同：`buildActions` 会把整条时间轴后移一个
//! `mouse_lead_ms`（那是给鼠标修饰键让出的提前量，不是音乐时间），
//! 试听从 0 开始，不需要这个偏移。

#include <vector>

#include "score.h"

namespace harmonica {

/// 一个音：相对曲首的开始时间、持续时长（毫秒），以及相对中音区 do 的半音数。
struct PreviewNote {
    double atMs = 0.0;
    double durMs = 0.0;
    int semitone = 0;

    friend bool operator==(const PreviewNote&, const PreviewNote&) = default;
};

/// 音高 → 相对中音区 do 的半音数。
///
/// 按简谱的常规读法折算：度数走大调音阶，`#` 加半音，音区按八度平移。
/// **只用于试听**：演奏路径不看这个值，它只认键位。
int semitoneOf(const Pitch& pitch);

/// 把曲谱展开成试听用的音符列表。
std::vector<PreviewNote> previewNotes(const Song& song);

}  // namespace harmonica
