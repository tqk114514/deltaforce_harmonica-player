#include "player.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "input.h"

namespace harmonica {
namespace {

/// 按键时长的数学下限（毫秒）。时值比 `noteGapMs` 还短的音会被压到这里，
/// 保证「按下」之后一定有对应的「松开」—— 否则键会卡在按下状态。
/// 这不是保底，是防止动作表出现「先松后按」的硬约束。
constexpr double MIN_KEY_HOLD_MS = 1.0;

struct PlayedNote {
    double startMs = 0.0;
    double durMs = 0.0;
    std::uint16_t vk = 0;
    std::vector<MouseButton> buttons;
};

/// 把演奏时刻和一个动作拼成表项：`at<MouseUp>(ms, MouseButton::Left)`
template <typename ActionT, typename ValueT>
TimedAction at(double atMs, ValueT value) {
    return TimedAction{atMs, ActionT{value}};
}

}  // namespace

std::vector<TimedAction> buildActions(const Song& song, const Config& cfg) {
    const double msPerTick = song.msPerTick();
    std::vector<PlayedNote> notes;
    // 时间轴整体后移一个 lead，保证第一个音的鼠标修饰键也能提前按下
    double cursor = static_cast<double>(cfg.timing.mouseLeadMs);

    for (const Event& event : song.events) {
        const double dur = static_cast<double>(ticksOf(event)) * msPerTick;
        if (const auto* note = std::get_if<NoteEvent>(&event)) {
            const auto [vk, buttons] = cfg.resolve(note->pitch);
            notes.push_back(PlayedNote{cursor, dur, vk, buttons});
        }
        cursor += dur;
    }

    const double lead = static_cast<double>(cfg.timing.mouseLeadMs);
    const double gap = static_cast<double>(cfg.timing.noteGapMs);

    std::vector<TimedAction> actions;
    // 修饰键按"差集"维护：本段仍然需要的键保持按住不撒手，
    // 只松开不再需要的、按下新需要的。
    std::vector<MouseButton> held;
    double lastOff = 0.0;
    // 上一段最后一个音符键的松开时刻。这一段修饰键要动，必须等它过去。
    double prevLastKeyUp = 0.0;
    std::size_t i = 0;

    while (i < notes.size()) {
        const std::vector<MouseButton> buttons = notes[i].buttons;
        const auto contains = [&buttons](MouseButton b) {
            return std::find(buttons.begin(), buttons.end(), b) != buttons.end();
        };
        std::size_t j = i;
        while (j + 1 < notes.size() && notes[j + 1].buttons == buttons) ++j;

        // 修饰键最早能动的时刻：前一个音彻底结束，再留 gap 的余量。
        // 早了的话，音符还在响、音高就变了，会发出一个很难听的滑音。
        const double earliest = i > 0 ? prevLastKeyUp + gap : 0.0;
        const double onAt = std::max(notes[i].startMs - lead, earliest);

        // 先松开本段用不到的，再按下本段新增的（同刻时 actionOrder 保证这个先后）。
        // 游戏按事件逐个处理：新的修饰键按下时旧的还没松，它就会同时看到
        // 两组修饰键（比如 左键+右键），这一段的音高就全错了。
        for (const MouseButton b : held) {
            if (!contains(b)) actions.push_back(at<MouseUp>(onAt, b));
        }
        for (const MouseButton b : buttons) {
            const bool alreadyHeld =
                std::find(held.begin(), held.end(), b) != held.end();
            if (!alreadyHeld) actions.push_back(at<MouseDown>(onAt, b));
        }
        held = buttons;

        // 音符键。段首那一个必须等修饰键建立起来（onAt + lead），
        // 否则开头一小段是按在错误的音区上，听起来就是跑调。
        double lastKeyUp = 0.0;
        for (std::size_t index = i; index <= j; ++index) {
            const PlayedNote& note = notes[index];
            // 段内也必须一个接一个：修饰键被推迟时（时值比松键间隔还短的音一定会遇到），
            // 只推迟段首会让后面的音跑到修饰键前面去，也可能早于本段第一个音 ——
            // 那既串了音，又按在错误的音区上。正常谱子里 `前一个音的松开 + gap`
            // 正好等于本音的记谱时刻，这条 max 不会改变任何东西。
            const double earliestDown =
                index == i ? onAt + lead : lastKeyUp + gap;
            const double downAt = std::max(note.startMs, earliestDown);
            // 完全按谱面时值：按住 = 时值 - 松键间隔
            const double upAt = downAt + std::max(note.durMs - gap, MIN_KEY_HOLD_MS);

            actions.push_back(at<KeyDown>(downAt, note.vk));
            actions.push_back(at<KeyUp>(upAt, note.vk));
            lastKeyUp = upAt;
        }
        prevLastKeyUp = lastKeyUp;
        // 修饰键的使命到**最后一个音的键松开**就结束了。
        // 以前这里多留了一个 lead（音结束再等一会儿才松），结果下一段的修饰键
        // 提前 lead 按下时旧的还没松 —— 游戏同时看到两组修饰键，音高判定作废。
        // 键和修饰键同一刻松开没问题：actionOrder 保证先松键、后松修饰键。
        lastOff = lastKeyUp;
        i = j + 1;
    }

    // 收尾：松开还按着的修饰键
    for (const MouseButton b : held) actions.push_back(at<MouseUp>(lastOff, b));

    std::stable_sort(actions.begin(), actions.end(), [](const TimedAction& a, const TimedAction& b) {
        if (a.atMs != b.atMs) return a.atMs < b.atMs;
        return actionOrder(a.action) < actionOrder(b.action);
    });
    return actions;
}

int actionOrder(const Action& action) {
    if (std::holds_alternative<KeyUp>(action)) return 0;
    if (std::holds_alternative<MouseUp>(action)) return 1;
    if (std::holds_alternative<MouseDown>(action)) return 2;
    return 3;  // KeyDown
}

QString describe(const Action& action) {
    return std::visit(
        [](const auto& a) -> QString {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, KeyDown>) {
                return QStringLiteral("key down %1").arg(keyName(a.vk));
            } else if constexpr (std::is_same_v<T, KeyUp>) {
                return QStringLiteral("key up %1").arg(keyName(a.vk));
            } else if constexpr (std::is_same_v<T, MouseDown>) {
                return QStringLiteral("mouse down %1").arg(buttonName(a.button));
            } else {
                return QStringLiteral("mouse up %1").arg(buttonName(a.button));
            }
        },
        action);
}

void dispatch(const Action& action) {
    std::visit(
        [](const auto& a) {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, KeyDown>) {
                input::keyDown(a.vk);
            } else if constexpr (std::is_same_v<T, KeyUp>) {
                input::keyUp(a.vk);
            } else if constexpr (std::is_same_v<T, MouseDown>) {
                input::mouseDown(a.button);
            } else {
                input::mouseUp(a.button);
            }
        },
        action);
}

ReleaseGuard::ReleaseGuard(std::vector<std::uint16_t> keys, bool active)
    : keys_(std::move(keys)), active_(active) {}

ReleaseGuard::~ReleaseGuard() {
    if (active_) input::releaseAll(keys_);
}

void play(const std::vector<TimedAction>& actions, const Config& cfg,
          std::atomic<bool>& stopRequested, bool dryRun,
          std::atomic<std::uint64_t>* progress) {
    // 无论正常结束、提前 return 还是抛异常，离开作用域时都会松开所有按键。
    // 漏掉这一步的后果很严重：鼠标键会卡在按下状态，导致整个系统点不动。
    const std::vector<std::uint16_t> keys = cfg.allKeys();
    const ReleaseGuard guard{keys, !dryRun};

    const auto base = std::chrono::steady_clock::now();
    std::size_t index = 0;

    while (true) {
        if (stopRequested.load(std::memory_order_relaxed)) break;
        if (index >= actions.size()) break;

        const auto target = base + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                        std::chrono::duration<double, std::milli>(actions[index].atMs));
        const auto now = std::chrono::steady_clock::now();
        if (now < target) {
            // 最多睡 20ms 就回头看一眼停止标志，睡死了 F10 就按不停
            const auto remainMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(target - now).count();
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<std::int64_t>(remainMs, 20)));
            continue;
        }

        const TimedAction& timed = actions[index];
        if (!dryRun) dispatch(timed.action);
        if (progress != nullptr) {
            progress->store(static_cast<std::uint64_t>(timed.atMs), std::memory_order_relaxed);
        }
        ++index;
    }
}

const std::vector<CalibrationGroup>& calibrationGroups() {
    // 每个音位都单独走 Config::resolve，不假设整组共用一套修饰键。
    static const std::vector<CalibrationGroup> groups{
        {Register::Mid, false, 7, QStringLiteral("不修饰")},
        {Register::Mid, true, 7, QStringLiteral("半音")},
        {Register::Low, false, 7, QStringLiteral("降调")},
        {Register::Low, true, 7, QStringLiteral("降调 + 半音")},
        {Register::High, false, 7, QStringLiteral("升调")},
        {Register::High, true, 7, QStringLiteral("升调 + 半音")},
        {Register::Top, false, 1, QStringLiteral("升调 + 逗号键")},
        {Register::Top, true, 1, QStringLiteral("升调 + 半音 + 逗号键")},
    };
    return groups;
}

std::vector<CalibrationStep> calibrationSteps(const Config& cfg) {
    std::vector<CalibrationStep> out;
    for (const CalibrationGroup& group : calibrationGroups()) {
        for (std::uint8_t degree = 1; degree <= group.count; ++degree) {
            const Pitch pitch{group.register_, degree, group.sharp};
            const auto [vk, buttons] = cfg.resolve(pitch);
            out.push_back(CalibrationStep{group.name, degree, vk, buttons});
        }
    }
    return out;
}

Song calibrationSong() {
    Song song;
    song.title = QStringLiteral("键位校准");
    song.bpm = 120.0;
    song.ticksPerBeat = 12;
    for (const CalibrationGroup& group : calibrationGroups()) {
        for (std::uint8_t degree = 1; degree <= group.count; ++degree) {
            song.events.push_back(NoteEvent{Pitch{group.register_, degree, group.sharp}, 14});
            song.events.push_back(RestEvent{5});
        }
    }
    return song;
}

}  // namespace harmonica
