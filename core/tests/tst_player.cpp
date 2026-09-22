#include <chrono>
#include <map>
#include <variant>

#include <QtTest>

#include "config.h"
#include "input.h"
#include "player.h"
#include "score.h"
#include "testhelpers.h"

using namespace harmonica;

namespace {

std::vector<TimedAction> actionsOf(const QString& score, const Config& cfg = {}) {
    const auto song = parseScore(score);
    Q_ASSERT_X(song.has_value(), "actionsOf",
               qUtf8Printable(song.has_value() ? QString() : song.error()));
    return buildActions(*song, cfg);
}

/// 某一类动作的全部表项，按时间顺序
template <typename T>
std::vector<const TimedAction*> ofType(const std::vector<TimedAction>& actions) {
    std::vector<const TimedAction*> out;
    for (const TimedAction& action : actions) {
        if (std::holds_alternative<T>(action.action)) out.push_back(&action);
    }
    return out;
}

template <typename T>
std::vector<const TimedAction*> forButton(const std::vector<TimedAction>& actions,
                                          MouseButton button) {
    std::vector<const TimedAction*> out;
    for (const TimedAction& action : actions) {
        if (const T* found = std::get_if<T>(&action.action); found && found->button == button) {
            out.push_back(&action);
        }
    }
    return out;
}

/// 按下必须有对应的松开，而且松开在按下之后；结束时不许留下按住状态。
/// 漏一条的后果是鼠标键卡在按下状态、整个系统点不动东西。
void expectEveryPressReleased(const std::vector<TimedAction>& actions) {
    std::map<std::uint16_t, double> heldKeys;
    std::map<int, double> heldButtons;
    for (const TimedAction& action : actions) {
        std::visit(
            [&](const auto& a) {
                using T = std::decay_t<decltype(a)>;
                if constexpr (std::is_same_v<T, KeyDown>) {
                    QVERIFY2(heldKeys.find(a.vk) == heldKeys.end(),
                             "同一个键被按了两次都没松");
                    heldKeys[a.vk] = action.atMs;
                } else if constexpr (std::is_same_v<T, KeyUp>) {
                    const auto found = heldKeys.find(a.vk);
                    QVERIFY2(found != heldKeys.end(), "松开了一个没按下的键");
                    QVERIFY2(action.atMs > found->second, "松开不晚于按下，键会卡住");
                    heldKeys.erase(found);
                } else if constexpr (std::is_same_v<T, MouseDown>) {
                    const int key = static_cast<int>(a.button);
                    QVERIFY2(heldButtons.find(key) == heldButtons.end(),
                             "同一个鼠标键被按了两次都没松");
                    heldButtons[key] = action.atMs;
                } else {
                    const int key = static_cast<int>(a.button);
                    const auto found = heldButtons.find(key);
                    QVERIFY2(found != heldButtons.end(), "松开了一个没按下的鼠标键");
                    QVERIFY2(action.atMs > found->second, "鼠标键松开不晚于按下，会卡住");
                    heldButtons.erase(found);
                }
            },
            action.action);
    }
    QVERIFY2(heldKeys.empty(), "演奏结束时还有音符键没松");
    QVERIFY2(heldButtons.empty(), "演奏结束时还有鼠标键没松");
}

/// 降调（左键）和升调（右键）永远不该同时按住：
/// 游戏同时看到两组修饰键时，这一整段音高判定会作废。
void expectOctaveModifiersNeverOverlap(const std::vector<TimedAction>& actions) {
    bool left = false;
    bool right = false;
    for (const TimedAction& action : actions) {
        if (const MouseDown* down = std::get_if<MouseDown>(&action.action); down) {
            if (down->button == MouseButton::Left) left = true;
            if (down->button == MouseButton::Right) right = true;
        } else if (const MouseUp* up = std::get_if<MouseUp>(&action.action); up) {
            if (up->button == MouseButton::Left) left = false;
            if (up->button == MouseButton::Right) right = false;
        }
        QVERIFY2(!(left && right), "左键和右键同时按住，这一段音高判定会作废");
    }
}

}  // namespace

class TestPlayer : public QObject {
    Q_OBJECT

private slots:
    void noteProducesDownThenUp();
    void lowRegisterAddsMouseModifierBeforeNote();
    void repeatedIdenticalNotesAreSeparated();
    void sharedModifierStaysHeldAcrossSegments();
    void octaveModifierIsHeldForWholeGroup();
    void modifierWaitsUntilPreviousNoteEnds();
    void nextSegmentNeverHoldsTwoModifierSets();
    void modifierReleaseNeverBeatsNoteRelease();
    void restProducesNoNoteActions();
    void firstNoteWaitsForTheModifierToSettle();
    void tinyNotesStillGetReleased();
    void nastyScoreKeepsEveryInvariant();
    void dryRunSendsNoInputButStillReportsProgress();
    void stopFlagEndsPlaybackAtOnce();
    void calibrationStepsLineUpWithSongNotes();
    void describeUsesKeyNames();
};

void TestPlayer::noteProducesDownThenUp() {
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nM1/12"));
    const auto downs = ofType<KeyDown>(actions);
    const auto ups = ofType<KeyUp>(actions);
    QCOMPARE(downs.size(), std::size_t{1});
    QCOMPARE(ups.size(), std::size_t{1});
    QVERIFY(ups[0]->atMs > downs[0]->atMs);
    // 中音区不需要鼠标修饰
    QCOMPARE(ofType<MouseDown>(actions).size(), std::size_t{0});
}

void TestPlayer::lowRegisterAddsMouseModifierBeforeNote() {
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nL1/12"));
    const auto mouseDowns = ofType<MouseDown>(actions);
    QCOMPARE(mouseDowns.size(), std::size_t{1});
    const auto downs = ofType<KeyDown>(actions);
    QCOMPARE(downs.size(), std::size_t{1});
    QVERIFY(mouseDowns[0]->atMs < downs[0]->atMs);
}

void TestPlayer::repeatedIdenticalNotesAreSeparated() {
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nM1/12 M1/12"));
    const auto keyEvents = ofType<KeyDown>(actions);
    const auto keyUps = ofType<KeyUp>(actions);
    // 同音重复必须 松-按-松-按
    QCOMPARE(keyEvents.size(), std::size_t{2});
    QCOMPARE(keyUps.size(), std::size_t{2});
    QVERIFY(keyUps[0]->atMs < keyEvents[1]->atMs);
    expectEveryPressReleased(actions);
}

void TestPlayer::sharedModifierStaysHeldAcrossSegments() {
    // L1 接 L1#：左键两段都要用，应该从头按到尾，只在中间补按中键
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nL1/12 L1#/12"));
    const auto left = forButton<MouseDown>(actions, MouseButton::Left);
    const auto leftUp = forButton<MouseUp>(actions, MouseButton::Left);
    QCOMPARE(left.size() + leftUp.size(), std::size_t{2});
    QCOMPARE(left.size(), std::size_t{1});
    // 中键只在第二段登场
    QCOMPARE(forButton<MouseDown>(actions, MouseButton::Middle).size(), std::size_t{1});
    expectEveryPressReleased(actions);
    expectOctaveModifiersNeverOverlap(actions);
}

void TestPlayer::octaveModifierIsHeldForWholeGroup() {
    // 连续 7 个低音：左键要按住整组，而不是每个音点一下
    const auto actions = actionsOf(QStringLiteral(
        "bpm=120\nticks_per_beat=12\n[score]\nL1/12 L2/12 L3/12 L4/12 L5/12 L6/12 L7/12"));
    const auto down = forButton<MouseDown>(actions, MouseButton::Left);
    const auto up = forButton<MouseUp>(actions, MouseButton::Left);
    QCOMPARE(down.size(), std::size_t{1});
    QCOMPARE(up.size(), std::size_t{1});
    // 7 个音，每音 500ms，总共约 3.5 秒
    REQUIRE_MSG(up[0]->atMs - down[0]->atMs > 3000.0,
                QStringLiteral("左键只按住了 %1 ms，应当是整组长按").arg(up[0]->atMs - down[0]->atMs));
}

void TestPlayer::modifierWaitsUntilPreviousNoteEnds() {
    // 低音短音紧接高音：左键要松开、右键要按下。
    // 修饰键的松开不能早于音符键的松开（否则最后一个音还在响，音高就变了）；
    // 同一刻松开没问题，actionOrder 保证先松键、后松修饰键。
    const Config cfg;
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=24\n[score]\nL1/3 H1/24"), cfg);

    const auto noteUps = ofType<KeyUp>(actions);
    const auto leftUp = forButton<MouseUp>(actions, MouseButton::Left);
    const auto rightDown = forButton<MouseDown>(actions, MouseButton::Right);
    QCOMPARE(noteUps.size(), std::size_t{2});
    QCOMPARE(leftUp.size(), std::size_t{1});
    QCOMPARE(rightDown.size(), std::size_t{1});

    QVERIFY2(leftUp[0]->atMs >= noteUps[0]->atMs,
             "左键在音符还响着的时候就松开了，会发出难听的滑音");
    QVERIFY2(rightDown[0]->atMs >= noteUps[0]->atMs + cfg.timing.noteGapMs,
             "右键按下得太早，前一个音还没留够间隔");
}

void TestPlayer::nextSegmentNeverHoldsTwoModifierSets() {
    const Config cfg;
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nL1/12 H1/12"), cfg);
    const auto leftUp = forButton<MouseUp>(actions, MouseButton::Left);
    const auto rightDown = forButton<MouseDown>(actions, MouseButton::Right);
    QCOMPARE(leftUp.size(), std::size_t{1});
    QCOMPARE(rightDown.size(), std::size_t{1});
    QVERIFY2(rightDown[0]->atMs >= leftUp[0]->atMs,
             "右键按下时左键还没松，游戏会同时看到两组修饰键");
    expectOctaveModifiersNeverOverlap(actions);
}

void TestPlayer::modifierReleaseNeverBeatsNoteRelease() {
    // 整组低音结束时，修饰键的松开也必须晚于最后一个音的松开
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=24\n[score]\nL1/3 L2/3 L3/24"));
    double lastNoteUp = 0.0;
    for (const TimedAction* up : ofType<KeyUp>(actions)) lastNoteUp = std::max(lastNoteUp, up->atMs);
    const auto leftUp = forButton<MouseUp>(actions, MouseButton::Left);
    QCOMPARE(leftUp.size(), std::size_t{1});
    QVERIFY2(leftUp[0]->atMs >= lastNoteUp, "左键在最后一个音之前就松开了");
}

void TestPlayer::restProducesNoNoteActions() {
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nR/12"));
    QCOMPARE(ofType<KeyDown>(actions).size(), std::size_t{0});
    QCOMPARE(ofType<MouseDown>(actions).size(), std::size_t{0});
    QVERIFY(actions.empty());
}

/// 段首那个音必须等修饰键建立起来，否则开头一小段是按在错误音区上的
void TestPlayer::firstNoteWaitsForTheModifierToSettle() {
    const Config cfg;
    const auto actions = actionsOf(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nL1/12"), cfg);
    const auto mouseDowns = ofType<MouseDown>(actions);
    const auto downs = ofType<KeyDown>(actions);
    QVERIFY(downs[0]->atMs >= mouseDowns[0]->atMs + cfg.timing.mouseLeadMs);
    // 时间轴整体后移一个 lead，第一个音不会在修饰键之前就响
    QCOMPARE(mouseDowns[0]->atMs, 0.0);
}

/// 时值比松键间隔还短的音：不许出现「先松后按」
void TestPlayer::tinyNotesStillGetReleased() {
    const auto actions = actionsOf(QStringLiteral("bpm=600\nticks_per_beat=12\n[score]\nM1/1 M1/1 H1/1 H1/1"));
    const auto downs = ofType<KeyDown>(actions);
    const auto ups = ofType<KeyUp>(actions);
    QCOMPARE(downs.size(), std::size_t{4});
    QCOMPARE(ups.size(), std::size_t{4});
    for (std::size_t i = 0; i < downs.size(); ++i) QVERIFY(ups[i]->atMs > downs[i]->atMs);
    expectEveryPressReleased(actions);
}

/// 一整段来回换音区、换升号、夹休止和极短音的谱子：三条硬不变量都得守住
void TestPlayer::nastyScoreKeepsEveryInvariant() {
    const auto actions = actionsOf(QStringLiteral(
        "bpm=180\nticks_per_beat=24\n[score]\n"
        "L1/24 L2/6 L3#/3 M4/24 H5/12 H6#/4 T1/24 T1#/6 R/12 M1/2 H2/24 L1/24 H1/24 M7/1"));
    QVERIFY(!actions.empty());
    expectEveryPressReleased(actions);
    expectOctaveModifiersNeverOverlap(actions);

    // 动作表按时间不递减，同刻按 order 排（先松键、再松修饰、再按修饰、最后按键）
    for (std::size_t i = 1; i < actions.size(); ++i) {
        QVERIFY2(actions[i].atMs >= actions[i - 1].atMs, "动作表没按时间排序");
        if (actions[i].atMs == actions[i - 1].atMs) {
            QVERIFY(actionOrder(actions[i].action) >= actionOrder(actions[i - 1].action));
        }
    }
}

/// 干跑不碰游戏：走完整条时间轴，但一个输入都不发
void TestPlayer::dryRunSendsNoInputButStillReportsProgress() {
    // 注意：这里只测 dry run。真发输入的分支不放进测试 —— 那会往用户的桌面上按键。
    const auto actions = actionsOf(QStringLiteral("bpm=600\nticks_per_beat=12\n[score]\nM1/12 H1/12 L1/12"));
    input::resetStats();
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> progress{0};

    play(actions, Config{}, stop, true, &progress);

    const auto [sent, failed] = input::stats();
    QCOMPARE(sent, quint64{0});
    QCOMPARE(failed, quint64{0});
    QVERIFY2(progress.load() > 0, "干跑也要报进度，悬浮窗才有东西显示");
    QCOMPARE(progress.load(), static_cast<std::uint64_t>(actions.back().atMs));
}

/// F10 的语义：置一个标志就得收工，不许把整首曲子的时间等完
void TestPlayer::stopFlagEndsPlaybackAtOnce() {
    const auto actions = actionsOf(QStringLiteral("bpm=60\nticks_per_beat=12\n[score]\nM1/12 M2/12 M3/12"));
    QVERIFY2(actions.back().atMs > 3000.0, "这首谱子太短，测不出提前收工");

    std::atomic<bool> stop{true};
    std::atomic<std::uint64_t> progress{0};
    const auto started = std::chrono::steady_clock::now();
    play(actions, Config{}, stop, true, &progress);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();

    REQUIRE_MSG(elapsed < 200, QStringLiteral("没停下来，花了 %1 ms").arg(elapsed));
    QCOMPARE(progress.load(), 0u);
}

/// 校准的音位列表必须和校准曲谱里的音符一一对应 ——
/// 界面按这个列表显示「现在吹的是什么」，错位了就会显示成别的组合。
void TestPlayer::calibrationStepsLineUpWithSongNotes() {
    const Config cfg;
    const auto steps = calibrationSteps(cfg);
    const auto song = calibrationSong();

    std::vector<Pitch> pitches;
    for (const Event& event : song.events) {
        if (const NoteEvent* note = std::get_if<NoteEvent>(&event); note) pitches.push_back(note->pitch);
    }
    QCOMPARE(steps.size(), pitches.size());
    QCOMPARE(calibrationGroups().size(), std::size_t{8});
    // 6 组 × 7 个音 + 超高音区 2 个（只有 do，原音和半音各一个）
    QCOMPARE(steps.size(), std::size_t{44});

    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto [vk, buttons] = cfg.resolve(pitches[i]);
        REQUIRE_MSG(steps[i].vk == vk,
                    QStringLiteral("%1 的按键对不上").arg(pitchName(pitches[i])));
        REQUIRE_MSG(steps[i].buttons == buttons,
                    QStringLiteral("%1 的修饰键对不上").arg(pitchName(pitches[i])));
    }
    // 校准谱走的是同一条 buildActions，所以 44 个音正好产生 44 个按下
    const auto actions = buildActions(song, cfg);
    QCOMPARE(ofType<KeyDown>(actions).size(), std::size_t{44});
    expectEveryPressReleased(actions);
    expectOctaveModifiersNeverOverlap(actions);
}

/// 干跑日志是给终端排查用的，键名要和 harmonica.ini 里写的一致
void TestPlayer::describeUsesKeyNames() {
    QCOMPARE(describe(Action{KeyDown{vk('Z')}}), QStringLiteral("key down Z"));
    QCOMPARE(describe(Action{KeyUp{0xBC}}), QStringLiteral("key up COMMA"));
    QCOMPARE(describe(Action{MouseDown{MouseButton::Left}}), QStringLiteral("mouse down left"));
    QCOMPARE(describe(Action{MouseUp{MouseButton::Right}}), QStringLiteral("mouse up right"));
    QVERIFY(actionOrder(Action{KeyUp{1}}) < actionOrder(Action{MouseUp{MouseButton::Left}}));
    QVERIFY(actionOrder(Action{MouseUp{MouseButton::Left}})
            < actionOrder(Action{MouseDown{MouseButton::Right}}));
    QVERIFY(actionOrder(Action{MouseDown{MouseButton::Right}}) < actionOrder(Action{KeyDown{1}}));
}

QTEST_GUILESS_MAIN(TestPlayer)
#include "tst_player.moc"
