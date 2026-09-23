#pragma once

//! 简谱编辑器的数据层：谱面模型、紧凑工程文件、`.dhs` 导入导出、以及给界面用的排版。
//!
//! 为什么在 C++ 而不是界面里：一份谱子的时值、连音线合并、反复展开、转调折算，
//! 导出时要算一遍、排版时要算一遍、试听还要算一遍 —— 三处各写一次迟早会对不上。
//! 这里只有**一份**实现，界面只管画和改。
//!
//! 工程文件是紧凑格式：只写和默认值不同的字段，一个中音四分音符就是 `{"degree":5}`。

#include <expected>
#include <optional>
#include <vector>

#include <QString>
#include <QVariantList>

namespace harmonica::editor {

/// 一拍分 24 tick：能表示十六分音符和附点，也能被三十二分整除
constexpr int TICKS_PER_BEAT = 24;

/// 一个倚音：画在主音左上角的小音符。演奏时先响，占的是主音前面的一段时间
struct Grace {
    int degree = 1;
    int octave = 0;  ///< -1 低音 / 0 中音 / 1 高音 / 2 超高音
    bool sharp = false;
    int dur = 16;    ///< 八分 / 十六分 / 三十二分

    friend bool operator==(const Grace&, const Grace&) = default;
};

struct Note {
    bool rest = false;
    int degree = 1;       ///< 1..7
    int octave = 0;       ///< -1 低音 / 0 中音 / 1 高音 / 2 超高音
    int dur = 4;          ///< 4 / 8 / 16 / 32 分音符
    bool dot = false;     ///< 附点，×1.5
    int hold = 0;         ///< 增时线，每条一拍（24 tick）
    bool sharp = false;
    std::vector<int> ties;   ///< 连音线指向的音符下标
    bool rstart = false;     ///< ‖:
    bool rend = false;       ///< :‖
    int mod = 0;             ///< 转调（半音数），实际作用于所在小节的第一个音
    std::optional<Grace> grace;

    friend bool operator==(const Note&, const Note&) = default;
};

struct Meta {
    QString title;
    int bpm = 120;
    int meter = 4;      ///< 拍号的分子，4 / 3 / 2
    bool barlines = true;
};

struct Score {
    Meta meta;
    std::vector<Note> notes;
};

/// 一个音的总时值 = 基本时值（含附点）+ 增时线
int ticksOf(const Note& note);
/// 时值（几分音符）换算成 tick 数
int ticksForDur(int dur);
/// 倚音实际占几个 tick：按时值算，但最多借主音的一半（借多了主音就没声了）
int graceTicksOf(const Note& note);

/// 只有「连到紧邻的下一个音、且音高相同」才合并时值，那是延音线；
/// 其它连法（跨音、音高不同）是圆滑线，只画在谱面上，不改变演奏
bool linkMerges(const std::vector<Note>& notes, int from, int to);

struct MergedNote {
    Note note;
    int totalTicks = 0;
};

/// 延音线合并。合并后 `note` 是这一串的开头
std::vector<MergedNote> mergeTies(const std::vector<Note>& notes);

/// ‖: ... :‖ 之间再走一遍。只有 ‖: 没有 :‖ 时反复到曲子结尾
std::vector<Note> expandRepeats(const std::vector<Note>& notes);

/// 最终要演奏（也是要导出）的音符序列。
///
/// 顺序很重要：**先**按原始顺序把每个音的移调量算成绝对值，**再**展开反复。
/// 反过来的话，反复抄出来的那一遍会把自己段内的转调标记再累加一次。
std::vector<Note> playableNotes(const std::vector<Note>& notes);

/// 第 i 个音所在小节的第一个音的下标。转调标记画在小节线上，
/// 所以点小节里任何位置都从该小节开头生效
int measureStartIndex(const std::vector<Note>& notes, int index, int meter);

/// 音位 -> MIDI 音高（M1 = C4 = 60）
int pitchToMidi(const Grace& pitch);
/// 音位 -> 频率（A4 = 440）。只给试听用
double freqOf(const Grace& pitch);
/// MIDI 音高 -> 音位，超出音域按八度折回；超高音区只保留 do
Grace midiToPitch(int midi);

/// 解析「前3=后5」这类关系，算出该升（降）多少半音。
/// 简谱是首调记法，这个关系本身就已经包含了移调量，不需要知道原调是什么。
/// 结果的绝对值一定在 6 个半音以内 —— 超过就说明该往另一个八度读，
/// 这时 `alternative` 给出等价的那个写法。
std::expected<int, QString> modFromRelation(const QString& text, int* alternative = nullptr);

/// 工程文件（`.score.json`）。紧凑格式 + 读回来补全默认值
QString toJson(const Score& score);
std::expected<Score, QString> fromJson(const QString& text);

/// 演奏谱（`.dhs`）。导出时连音线、反复、转调、倚音都已展开成实际音符：
/// 倚音写成一个普通短音，并从主音借走同样多的时间，所以整首曲子的总时长不变
QString toDhs(const Score& score);
/// 读一份 `.dhs`：只能恢复音符，那四类标记恢复不了
std::expected<Score, QString> fromDhs(const QString& text);

/// 排版：界面按这个画，不许自己再算一遍小节和减时线
struct PlacedNote {
    int index = 0;         ///< 对应 notes 里的下标
    int measure = 0;       ///< 第几个小节（从 0 起）
    bool measureStart = false;
    int beamLevels = 0;    ///< 几条减时线（八分 1 条、十六分 2 条…）
    /// 和**前一个**音共用减时线：同一拍内、层数相同、且不是新小节
    bool beamSharedWithPrev = false;
    int tieTargets;        ///< 从这个音出发的连音线条数（圆滑线也要画）
    bool isRest = false;
    bool sharp = false;
    int octave = 0;
    int degree = 1;
    int holdBars = 0;      ///< 增时线条数
    bool dot = false;
    bool rstart = false;
    bool rend = false;
    QString modLabel;      ///< 转调标记，如 "+3"；空 = 这一小节没转调
    std::optional<Grace> grace;
};

std::vector<PlacedNote> layout(const Score& score);

/// 给 QML 的形状（字段名小驼峰）
QVariantList layoutAsVariantList(const std::vector<PlacedNote>& placed);

}  // namespace harmonica::editor
