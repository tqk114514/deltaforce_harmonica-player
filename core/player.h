#pragma once

//! 时序调度：把曲谱展开成带绝对时间戳的动作表，再按表执行。
//!
//! 设计要点：
//! 1. 时间戳全部相对演奏起点计算（绝对时间轴），不累加 sleep 误差；
//! 2. 相邻同修饰的音符合并鼠标键的按下/松开，减少鼠标事件；
//! 3. 完全按谱面时值发按键，不做顺延或延长；
//! 4. 任何退出路径都释放按键。

#include <atomic>
#include <cstdint>
#include <variant>
#include <vector>

#include <QString>

#include "config.h"
#include "score.h"

namespace harmonica {

struct KeyDown {
    std::uint16_t vk = 0;
};
struct KeyUp {
    std::uint16_t vk = 0;
};
struct MouseDown {
    MouseButton button = MouseButton::Left;
};
struct MouseUp {
    MouseButton button = MouseButton::Left;
};

/// 一个动作。用 variant 而不是「类型标签 + 两个字段」，构造不出「KeyDown 带鼠标键」这种脏值。
using Action = std::variant<KeyDown, KeyUp, MouseDown, MouseUp>;

struct TimedAction {
    /// 相对演奏起点的时刻（毫秒）
    double atMs = 0.0;
    Action action;
};

/// 把曲谱展开成动作表。
///
/// 完全按谱面时值来：不缩短、不顺延、不延长。只有两类顺序约束会推迟动作 ——
/// 修饰键的切换必须等前一个音彻底松开，段首音符必须等修饰键建立起来，
/// 否则会发出滑音或整段跑调。这两条不改变总时长，只决定先后。
std::vector<TimedAction> buildActions(const Song& song, const Config& cfg);

/// 同一时刻的执行顺序，小的先执行。
/// 松开音符键排最前：先掐断上一个音，再动修饰键，避免瞬间串音。
/// **松开旧修饰键要排在按下新修饰键前面**：修饰键的使命到音符键松开就结束了，
/// 要是新的先按、旧的后松，游戏会同时看到两组修饰键（比如 左键+右键），
/// 音高判定直接作废 —— 表现就是那一整段音区全都触发不了。
int actionOrder(const Action& action);

/// 把动作描述成一行文字。**给日志和干跑用的**，所以是英文 ——
/// 界面要展示动作时会自己按 `Action` 渲染（图标、键帽），不复用这里的措辞。
QString describe(const Action& action);

/// 发送一个动作（不改动作文本身）
void dispatch(const Action& action);

/// 作用域结束时松开所有按键，异常展开时同样生效。
class ReleaseGuard {
public:
    ReleaseGuard(std::vector<std::uint16_t> keys, bool active);
    ReleaseGuard(const ReleaseGuard&) = delete;
    ReleaseGuard& operator=(const ReleaseGuard&) = delete;
    ~ReleaseGuard();

private:
    std::vector<std::uint16_t> keys_;
    bool active_ = false;
};

/// 按动作表演奏。`stopRequested` 置 true 就收工；dryRun 时不发送任何输入。
/// progress 用于把当前演奏位置（毫秒）回传给界面轮询。
void play(const std::vector<TimedAction>& actions, const Config& cfg,
          std::atomic<bool>& stopRequested, bool dryRun,
          std::atomic<std::uint64_t>* progress = nullptr);

// ---------------------------------------------------------------- 键位校准

/// 校准的一组：音区、是否升半音、该组吹几个音、显示名
struct CalibrationGroup {
    Register register_;
    bool sharp;
    std::uint8_t count;
    QString name;
};

/// 校准顺序：从不修饰开始，逐步叠加修饰；超高音区只有 do，所以只吹 1 个音。
const std::vector<CalibrationGroup>& calibrationGroups();

/// 校准过程中的一个音位，供界面显示「现在吹的是哪个组合」
struct CalibrationStep {
    /// 组名，如「降调 + 半音」
    QString group;
    std::uint8_t degree = 1;
    /// 这个音位实际按下的键
    std::uint16_t vk = 0;
    /// 这个音位实际要按住的鼠标键
    std::vector<MouseButton> buttons;

    friend bool operator==(const CalibrationStep&, const CalibrationStep&) = default;
};

/// 按 [`calibrationGroups`] 的顺序列出全部校准音位。
/// 顺序与 [`calibrationSong`] 中的音符严格一一对应。
std::vector<CalibrationStep> calibrationSteps(const Config& cfg);

/// 校准用的曲谱：把每个音位依次吹一遍。
/// 一个音 14 tick、之间隔 5 tick 休止；120 BPM、12 tick/拍 下每音约 0.58 秒。
///
/// 它走的是和正式演奏同一套 [`buildActions`]，所以校准时的修饰键合并、提前量、
/// 松开顺序都与演奏一致，不会出现「校准听着对、演奏却不对」。
Song calibrationSong();

}  // namespace harmonica
