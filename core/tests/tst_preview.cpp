#include <QtTest>

#include "preview.h"
#include "score.h"
#include "testhelpers.h"

using namespace harmonica;

namespace {

constexpr PreviewNote noteAt(int index, const std::vector<PreviewNote>& notes) {
    return notes.at(static_cast<std::size_t>(index));
}

/// 毫秒是浮点算出来的，别用 QCOMPARE 卡死 —— 差在小数点后十位以内就算对
void expectClose(double actual, double want, const char* why) {
    QVERIFY2(qAbs(actual - want) < 1e-9, why);
}

}  // namespace

class TestPreview : public QObject {
    Q_OBJECT

private slots:
    void semitonesFollowTheScale();
    void semitonesShiftByOctave();
    void previewTimelineMatchesScore();
    void restsTakeTimeButProduceNoNote();
    void sharpAddsOneSemitone();
};

void TestPreview::semitonesFollowTheScale() {
    QCOMPARE(semitoneOf(Pitch{Register::Mid, 1, false}), 0);
    QCOMPARE(semitoneOf(Pitch{Register::Mid, 3, false}), 4);
    QCOMPARE(semitoneOf(Pitch{Register::Mid, 5, false}), 7);
    QCOMPARE(semitoneOf(Pitch{Register::Mid, 7, false}), 11);
    QCOMPARE(semitoneOf(Pitch{Register::Mid, 1, true}), 1);
}

void TestPreview::semitonesShiftByOctave() {
    QCOMPARE(semitoneOf(Pitch{Register::Low, 1, false}), -12);
    QCOMPARE(semitoneOf(Pitch{Register::High, 1, false}), 12);
    QCOMPARE(semitoneOf(Pitch{Register::Top, 1, false}), 24);
    // 超高音区只按八度平移，度数照常折算（解析器根本不许写 T7，这里防的是别处手滑）
    QCOMPARE(semitoneOf(Pitch{Register::Top, 7, false}), 24 + 11);
}

/// 试听的音高折算不许跑出十二度之外：度数按大调音阶走，最多到 7
void TestPreview::sharpAddsOneSemitone() {
    for (int degree = 1; degree <= 7; ++degree) {
        const auto plain = semitoneOf(Pitch{Register::Mid, static_cast<std::uint8_t>(degree), false});
        const auto sharp = semitoneOf(Pitch{Register::Mid, static_cast<std::uint8_t>(degree), true});
        QCOMPARE(sharp - plain, 1);
    }
}

/// 试听的时间轴必须和 Song 报的总时长对得上 ——
/// 对不上就说明时值又出现了第二套算法。
void TestPreview::previewTimelineMatchesScore() {
    const auto song = parseScore(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nM1/12 R/12 M1/24"));
    REQUIRE_OK(song);
    const auto notes = previewNotes(*song);

    QCOMPARE(notes.size(), std::size_t{2});
    // 120 BPM、12 tick/拍 → 一拍 500 ms
    expectClose(noteAt(0, notes).atMs, 0.0, "第一个音要从 0 开始：试听不吃鼠标修饰键的提前量");
    expectClose(noteAt(0, notes).durMs, 500.0, "12 tick 是一拍");
    expectClose(noteAt(1, notes).atMs, 1000.0, "前面有一个 12 tick 的休止");
    expectClose(noteAt(1, notes).durMs, 1000.0, "24 tick 是两拍");
    expectClose(noteAt(1, notes).atMs + noteAt(1, notes).durMs, song->durationMs(),
                "试听总时长和谱面总时长必须一致");
}

void TestPreview::restsTakeTimeButProduceNoNote() {
    const auto song = parseScore(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nR/24 M1/12"));
    REQUIRE_OK(song);
    const auto notes = previewNotes(*song);
    QCOMPARE(notes.size(), std::size_t{1});
    expectClose(notes[0].atMs, 1000.0, "休止占的时间要算进偏移");
}

QTEST_GUILESS_MAIN(TestPreview)
#include "tst_preview.moc"
