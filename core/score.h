#pragma once

//! 曲谱解析。格式与社区 .dhs 一致：
//!
//! ```text
//! # 注释
//! title=小星星
//! bpm=96
//! ticks_per_beat=12
//!
//! [score]
//! M1/12 M1/12 M5/12 M5/12 M6/12 M6/12 M5/24
//! R/12 L5/6 H3#/3
//! ```
//!
//! 事件 = `音位/持续tick数`；音位为 `区+度数[#]`，区为 L(低) M(中) H(高) T(超高)，
//! 度数 1..7，`#` 升半音，`R` 为休止。tick 时长 = 60000 / bpm / ticks_per_beat 毫秒。

#include <expected>
#include <cstdint>
#include <variant>
#include <vector>

#include <QString>

namespace harmonica {

enum class Register {
    Low,
    Mid,
    High,
    /// 超高音区：只有 do（简谱里 1 上面两个点）
    Top,
};

struct Pitch {
    Register register_ = Register::Mid;
    /// 1..=7
    std::uint8_t degree = 1;
    bool sharp = false;

    friend bool operator==(const Pitch&, const Pitch&) = default;
};

/// 一个音符事件
struct NoteEvent {
    Pitch pitch;
    std::uint32_t ticks = 0;
};

/// 一个休止事件
struct RestEvent {
    std::uint32_t ticks = 0;
};

using Event = std::variant<NoteEvent, RestEvent>;

/// 事件占几个 tick
std::uint32_t ticksOf(const Event& event);

/// 音位写成谱面上的记号，如 `M4#`
QString pitchName(const Pitch& pitch);

struct Song {
    QString title;
    QString artist;
    double bpm = 72.0;
    std::uint32_t ticksPerBeat = 12;
    std::vector<Event> events;

    double msPerTick() const;
    std::uint32_t totalTicks() const;
    double durationMs() const;
    std::size_t noteCount() const;
};

/// 解析一段 .dhs 文本。开头的 BOM 在这里剥掉 —— 编辑器导出的文件常带 BOM，
/// 留着会让第一行元信息解析失败。所有调用方（曲库、社区试听、导出）共用这一份。
std::expected<Song, QString> parseScore(const QString& text);

}  // namespace harmonica
