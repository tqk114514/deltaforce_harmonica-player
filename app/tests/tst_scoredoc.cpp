#include <QJsonDocument>
#include <QtTest>

#include "testhelpers.h"
#include "scoredoc.h"
#include "score.h"

using namespace harmonica;
using namespace harmonica::editor;

namespace {

Note mid(int degree, int dur = 4, bool sharp = false) {
    Note note;
    note.degree = degree;
    note.dur = dur;
    note.sharp = sharp;
    return note;
}

Note rest(int dur = 4) {
    Note note;
    note.rest = true;
    note.dur = dur;
    return note;
}

Score scoreOf(std::vector<Note> notes, int meter = 4, int bpm = 120) {
    Score score;
    score.meta.title = QStringLiteral("测试");
    score.meta.meter = meter;
    score.meta.bpm = bpm;
    score.notes = std::move(notes);
    return score;
}

/// 音位写成谱面记号，比对用
QString noteName(const Note& note) {
    const Register reg = note.octave <= -1 ? Register::Low
                       : note.octave == 0 ? Register::Mid
                       : note.octave == 1 ? Register::High : Register::Top;
    return pitchName(Pitch{reg, static_cast<std::uint8_t>(note.degree), note.sharp});
}

/// 导出谱子里所有 token 的 tick 总和
int dhsTicks(const QString& text) {
    int total = 0;
    for (const QString& line : text.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.contains(QLatin1Char('/'))) continue;
        for (const QString& token : trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const int slash = token.indexOf(QLatin1Char('/'));
            if (slash < 0) continue;
            total += token.sliced(slash + 1).toInt();
        }
    }
    return total;
}

}  // namespace

class TestScoreDoc : public QObject {
    Q_OBJECT

private slots:
    void tickMathMatchesTheNotation();
    void projectFileIsCompactAndRoundTrips();
    void projectFileRejectsForeignFormat();
    void tiesMergeOnlySamePitchAdjacent();
    void repeatsExpandAndRemapTies();
    void transpositionIsCountedBeforeRepeats();
    void transpositionFoldsBackIntoRange();
    void relationParsingAndFolding();
    void graceBorrowsFromTheMainNote();
    void exportedScoreIsPlayableAndSameLength();
    void dhsImportRestoresDurations();
    void layoutGroupsBeamsByBeat();
};

void TestScoreDoc::tickMathMatchesTheNotation() {
    QCOMPARE(ticksOf(mid(1)), 24);
    QCOMPARE(ticksOf(mid(1, 8)), 12);
    QCOMPARE(ticksOf(mid(1, 16)), 6);
    QCOMPARE(ticksOf(mid(1, 32)), 3);
    Note dotted = mid(1);
    dotted.dot = true;
    QCOMPARE(ticksOf(dotted), 36);
    Note held = mid(1);
    held.hold = 24;
    QCOMPARE(ticksOf(held), 48);
    QCOMPARE(ticksForDur(16), 6);
}

/// 工程文件只写和默认值不同的字段；读回来必须补全成同一个谱子
void TestScoreDoc::projectFileIsCompactAndRoundTrips() {
    // 一个中音四分音符就是 {"degree":5}，八分休止符是 {"dur":8,"rest":true}
    QCOMPARE(QJsonDocument::fromJson(toJson(scoreOf({mid(5)})).toUtf8())
                 .object()
                 .value(QStringLiteral("notes"))
                 .toArray()
                 .at(0)
                 .toObject()
                 .size(), 1);
    const QString restText = toJson(scoreOf({rest(8)}));
    QVERIFY(restText.contains(QStringLiteral("\"rest\": true")));
    QVERIFY(restText.contains(QStringLiteral("\"dur\": 8")));
    QVERIFY(!restText.contains(QStringLiteral("\"degree\"")));

    Note fancy = mid(3, 16, true);
    fancy.octave = -1;
    fancy.dot = true;
    fancy.hold = 24;
    fancy.ties = {1};
    fancy.rstart = true;
    fancy.mod = 3;
    fancy.grace = Grace{2, 1, false, 8};
    Note tail = mid(4);
    tail.rend = true;

    const Score original = scoreOf({mid(5), rest(8), fancy, tail});
    const QString text = toJson(original);
    QVERIFY2(text.contains(QStringLiteral("\"degree\": 5")), qPrintable(text));
    QVERIFY2(!text.contains(QStringLiteral("\"sharp\": false")), "默认值不该写进文件");
    QVERIFY(text.contains(QStringLiteral("\"format\": \"harmonica-score-v1\"")));

    const auto back = fromJson(text);
    REQUIRE_OK(back);
    QCOMPARE(back->notes.size(), std::size_t{4});
    QVERIFY(back->notes == original.notes);
    QCOMPARE(back->meta.title, original.meta.title);
    QCOMPARE(back->meta.meter, original.meta.meter);
}

void TestScoreDoc::projectFileRejectsForeignFormat() {
    QVERIFY(!fromJson(QStringLiteral("{\"notes\":[]}")).has_value());
    QVERIFY(!fromJson(QStringLiteral("not json")).has_value());
    QVERIFY(!fromJson(QStringLiteral("{\"format\":\"harmonica-score-v9\",\"notes\":[]}")).has_value());
}

void TestScoreDoc::tiesMergeOnlySamePitchAdjacent() {
    // 7-7-7 一路连下去：合并成一个三倍长的音
    Note a = mid(7);
    a.ties = {1};
    Note b = mid(7);
    b.ties = {2};
    const Score chained = scoreOf({a, b, mid(7)});
    auto merged = mergeTies(chained.notes);
    QCOMPARE(merged.size(), std::size_t{1});
    QCOMPARE(merged[0].totalTicks, 72);

    // 音高不同的连法是圆滑线：画在谱面上，不改变演奏
    Note x = mid(7);
    x.ties = {1};
    Note y = mid(6);
    const Score slur = scoreOf({x, y});
    auto separate = mergeTies(slur.notes);
    QCOMPARE(separate.size(), std::size_t{2});
    QCOMPARE(separate[0].totalTicks, 24);
    // 隔一个音连过去也不算延音线
    QVERIFY(!linkMerges(slur.notes, 0, 2));
}

void TestScoreDoc::repeatsExpandAndRemapTies() {
    Note start = mid(1);
    start.rstart = true;
    Note inner = mid(2);
    inner.ties = {2};              // 连到段内的下一个音
    Note end = mid(2);
    end.rend = true;
    Note tail = mid(5);

    const auto expanded = expandRepeats(scoreOf({start, inner, end, tail}).notes);
    // 原样 4 个 + 抄一遍 (rstart..rend) 的 3 个
    QCOMPARE(expanded.size(), std::size_t{7});
    QVERIFY(!expanded[3].rstart);  // 抄出来的那一遍不许再带反复记号
    QVERIFY(!expanded[5].rend);
    // 段内的连音线索引要映射到副本上：原段是 0..2，副本从 3 起，2 -> 5
    QCOMPARE(expanded[4].ties, (std::vector<int>{5}));
}

/// 转调标记在反复段里面时，抄出来的那一遍不许把自己段内的量再累加一次
void TestScoreDoc::transpositionIsCountedBeforeRepeats() {
    Note first = mid(1);
    Note start = mid(2);
    start.rstart = true;
    Note shiftedNote = mid(3);
    shiftedNote.mod = 3;           // 段内转 +3：M3 -> M5
    Note end = mid(4);
    end.rend = true;

    const auto playable = playableNotes(scoreOf({first, start, shiftedNote, end}).notes);
    // 原样 4 个 + 段内 (start..end) 的 3 个副本
    QCOMPARE(playable.size(), std::size_t{7});
    QCOMPARE(noteName(playable[2]), QStringLiteral("M5"));
    // 复制出来的那一格必须还是 M5，不是被加了两次的 M7
    QCOMPARE(noteName(playable[5]), QStringLiteral("M5"));
}

void TestScoreDoc::transpositionFoldsBackIntoRange() {
    Note high = mid(7);
    high.octave = 1;               // H7
    high.mod = 5;                  // 再往上五度：超出音域，按八度折回
    const auto playable = playableNotes(scoreOf({high}).notes);
    QVERIFY(playable[0].octave <= 2);
    // 移调后仍然是一个合法的音位（超高音区只剩 do）
    QVERIFY(playable[0].degree >= 1 && playable[0].degree <= 7);

    QCOMPARE(midiToPitch(60).degree, 1);
    QCOMPARE(midiToPitch(60).octave, 0);
    QCOMPARE(midiToPitch(72).octave, 1);
    QCOMPARE(midiToPitch(84).octave, 2);
    QCOMPARE(midiToPitch(84).degree, 1);   // 超高音区只有 do
    QCOMPARE(midiToPitch(48).octave, -1);
    QCOMPARE(pitchToMidi(Grace{1, 0, false, 4}), 60);
    QCOMPARE(pitchToMidi(Grace{1, 0, true, 4}), 61);
}

void TestScoreDoc::relationParsingAndFolding() {
    auto up = modFromRelation(QStringLiteral("前3=后5"));
    REQUIRE_OK(up);
    QCOMPARE(*up, 3);

    int alternative = 0;
    auto down = modFromRelation(QStringLiteral("前5=后1"), &alternative);
    REQUIRE_OK(down);
    QCOMPARE(*down, 5);            // -7 折成 +5（差一个八度）
    QCOMPARE(alternative, -7);

    QCOMPARE(*modFromRelation(QStringLiteral("4")), 4);
    QCOMPARE(*modFromRelation(QStringLiteral("-2")), -2);
    QCOMPARE(*modFromRelation(QStringLiteral("前1=后1")), 0);
    QVERIFY(!modFromRelation(QStringLiteral("胡说")).has_value());
    QVERIFY(!modFromRelation(QString()).has_value());
}

/// README 里那条：`M5/24` 配十六分倚音 `H1` → `H1/6 M5/18`
void TestScoreDoc::graceBorrowsFromTheMainNote() {
    Note main = mid(5);
    main.grace = Grace{1, 1, false, 16};
    QCOMPARE(graceTicksOf(main), 6);

    const QString text = toDhs(scoreOf({main}));
    QVERIFY2(text.contains(QStringLiteral("H1/6 M5/18")), qPrintable(text));

    // 主音太短就把倚音压短：最多借一半，总时长不变
    Note tiny = mid(5, 16);        // 6 tick
    tiny.grace = Grace{1, 1, false, 16};
    QCOMPARE(graceTicksOf(tiny), 3);
    const QString tinyText = toDhs(scoreOf({tiny}));
    QVERIFY2(tinyText.contains(QStringLiteral("H1/3 M5/3")), qPrintable(tinyText));
}

/// 导出的东西必须能被演奏器读，而且时长和编辑器自己算的一致 ——
/// 这就是「时值只有一份实现」的落地检查
void TestScoreDoc::exportedScoreIsPlayableAndSameLength() {
    Note a = mid(1);
    a.ties = {1};
    Note b = mid(1);
    b.dot = true;
    Note c = rest(8);
    Note d = mid(6);
    d.octave = -1;
    d.sharp = true;
    Note e = mid(1);
    e.octave = 2;                  // 超高音 do
    const Score original = scoreOf({a, b, c, d, e});

    int expectedTicks = 0;
    for (const auto& merged : mergeTies(playableNotes(original.notes))) {
        expectedTicks += merged.totalTicks;
    }

    const QString text = toDhs(original);
    const auto parsed = parseScore(text);
    // 导出的东西首先得能被演奏器读
    REQUIRE_OK(parsed);
    QCOMPARE(dhsTicks(text), expectedTicks);
    QCOMPARE(static_cast<int>(parsed->totalTicks()), expectedTicks);
    QCOMPARE(parsed->noteCount(), std::size_t{3});   // 休止符不算音
}

void TestScoreDoc::dhsImportRestoresDurations() {
    const auto imported = fromDhs(QStringLiteral(
        "title=回来的\nbpm=96\nticks_per_beat=24\n[score]\nM1/24 M5/18 R/12 H1#/6 L1/48"));
    REQUIRE_OK(imported);
    QCOMPARE(imported->meta.title, QStringLiteral("回来的"));
    QCOMPARE(imported->notes.size(), std::size_t{5});
    QCOMPARE(ticksOf(imported->notes[0]), 24);
    QCOMPARE(ticksOf(imported->notes[1]), 18);   // 附点八分
    QVERIFY(imported->notes[1].dot);
    QVERIFY(imported->notes[2].rest);
    QCOMPARE(imported->notes[3].degree, 1);
    QVERIFY(imported->notes[3].sharp);
    QCOMPARE(imported->notes[3].octave, 1);
    QCOMPARE(ticksOf(imported->notes[4]), 48);
    QVERIFY(!fromDhs(QStringLiteral("[score]\nX1/12")).has_value());
    QVERIFY(!fromDhs(QStringLiteral("title=空\n")).has_value());
}

void TestScoreDoc::layoutGroupsBeamsByBeat() {
    // 一拍里两个八分共用一条线；跨拍断开
    const Score score = scoreOf({mid(1, 8), mid(2, 8), mid(3, 8), mid(4, 8)}, 4, 120);
    const auto placed = layout(score);
    QCOMPARE(placed.size(), std::size_t{4});
    QCOMPARE(placed[0].beamLevels, 1);
    QVERIFY(!placed[0].beamSharedWithPrev);
    QVERIFY(placed[1].beamSharedWithPrev);
    QVERIFY(!placed[2].beamSharedWithPrev);
    QVERIFY(placed[3].beamSharedWithPrev);

    // 四分音符没有减时线；小节号按拍号走
    const Score quarters = scoreOf({mid(1), mid(2), mid(3), mid(4), mid(5)}, 4, 120);
    const auto laid = layout(quarters);
    QCOMPARE(laid[0].beamLevels, 0);
    QCOMPARE(laid[4].measure, 1);
    QVERIFY(laid[4].measureStart);
}

QTEST_GUILESS_MAIN(TestScoreDoc)
#include "tst_scoredoc.moc"
