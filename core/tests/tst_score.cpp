#include <variant>

#include <QtTest>

#include "score.h"
#include "testhelpers.h"

using namespace harmonica;

/// 取第 i 个事件的音位。不是音符就直接抛 —— 那是解析器的 bug，不该悄悄比出个错值。
#define PITCH_AT(song, i) (std::get<NoteEvent>((song).events.at(i)).pitch)
/// 音位比字符串：报错信息能直接看出差在哪个记号上
#define QCOMPARE_PITCH(actual, expected) QCOMPARE(pitchName(actual), QStringLiteral(expected))

class TestScore : public QObject {
    Q_OBJECT

private slots:
    void parses_header_and_notes();
    void defaults_when_header_missing();
    void rejects_bad_tokens();
    void top_register_only_allows_do();
    void tick_timing();
    void strips_bom();
    void parses_without_score_marker();
    void ignores_comments_and_crlf();
    void error_names_the_line();
    void pitch_prints_as_notation();
    void empty_score_is_an_error();
};

void TestScore::parses_header_and_notes() {
    const auto song = parseScore(
        QStringLiteral("title=小星星\nbpm=96\nticks_per_beat=12\n[score]\nM1/12 M5/6 R/6 H3#/3"));
    REQUIRE_OK(song);
    QCOMPARE(song->title, QStringLiteral("小星星"));
    QCOMPARE(song->bpm, 96.0);
    QCOMPARE(song->events.size(), std::size_t{4});
    QCOMPARE_PITCH(PITCH_AT(*song, 3), "H3#");
    QCOMPARE(song->noteCount(), std::size_t{3});
    QVERIFY(std::holds_alternative<RestEvent>(song->events.at(2)));
    QCOMPARE(ticksOf(song->events.at(2)), 6u);
    QCOMPARE(ticksOf(song->events.at(0)), 12u);
}

void TestScore::defaults_when_header_missing() {
    const auto song = parseScore(QStringLiteral("[score]\nM1/12"));
    REQUIRE_OK(song);
    QCOMPARE(song->title, QStringLiteral("未命名"));
    QCOMPARE(song->artist, QString());
    QCOMPARE(song->bpm, 72.0);
    QCOMPARE(song->ticksPerBeat, 12u);
}

void TestScore::rejects_bad_tokens() {
    QVERIFY(!parseScore(QStringLiteral("[score]\nX1/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM9/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM1")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM1/0")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM1/abc")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM1S/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\n/12")).has_value());
    // 升号后面不许拖别的字符
    QVERIFY(!parseScore(QStringLiteral("[score]\nM1#x/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("[score]\nM0/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("bpm=abc\n[score]\nM1/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("bpm=-4\n[score]\nM1/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("bpm=0\n[score]\nM1/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("ticks_per_beat=0\n[score]\nM1/12")).has_value());
    QVERIFY(!parseScore(QStringLiteral("ticks_per_beat=abc\n[score]\nM1/12")).has_value());
}

void TestScore::top_register_only_allows_do() {
    QVERIFY(parseScore(QStringLiteral("[score]\nT1/12")).has_value());
    // 超高音区只有 do
    QVERIFY(!parseScore(QStringLiteral("[score]\nT2/12")).has_value());
    const auto song = parseScore(QStringLiteral("[score]\nT1#/12"));
    REQUIRE_OK(song);
    QCOMPARE_PITCH(PITCH_AT(*song, 0), "T1#");
}

void TestScore::tick_timing() {
    const auto song = parseScore(QStringLiteral("bpm=120\nticks_per_beat=12\n[score]\nM1/12"));
    REQUIRE_OK(song);
    // 120 BPM 下每拍 500ms，一拍 12 tick => 每 tick 41.67ms
    QVERIFY(qAbs(song->msPerTick() - 41.6667) < 0.01);
    QVERIFY(qAbs(song->durationMs() - 500.0) < 0.01);
    QCOMPARE(song->totalTicks(), 12u);
}

/// 编辑器导出的谱子常带 BOM，留着会让第一行元信息解析失败
void TestScore::strips_bom() {
    const QString body = QStringLiteral("title=鸟之诗\nbpm=100\n[score]\nM1/12");
    const auto plain = parseScore(body);
    const auto withBom = parseScore(QString::fromUtf8("\xEF\xBB\xBF") + body);
    REQUIRE_OK(plain);
    REQUIRE_OK(withBom);
    QCOMPARE(withBom->title, plain->title);
    QCOMPARE(withBom->bpm, plain->bpm);
    QCOMPARE(withBom->events.size(), plain->events.size());
}

/// 没有 [score] 标记时，非 key=value 的行也当作谱面
void TestScore::parses_without_score_marker() {
    const auto song = parseScore(QStringLiteral("title=小星星\nbpm=96\nM1/12 M1/12 M5/12"));
    REQUIRE_OK(song);
    QCOMPARE(song->events.size(), std::size_t{3});
    QCOMPARE(song->title, QStringLiteral("小星星"));
}

void TestScore::ignores_comments_and_crlf() {
    const auto song = parseScore(
        QStringLiteral("# 注释\r\n; 也是注释\r\n\r\ntitle=死别\r\n[score]\r\nM1/12\r\n L5/6\r\n"));
    REQUIRE_OK(song);
    QCOMPARE(song->title, QStringLiteral("死别"));
    QCOMPARE(song->events.size(), std::size_t{2});
    // [score] 之后带前导空格的行也要能读
    QCOMPARE_PITCH(PITCH_AT(*song, 1), "L5");
}

/// 报错要带行号 —— 一首几百行的谱子，只说「看不懂」等于没说
void TestScore::error_names_the_line() {
    const auto song = parseScore(QStringLiteral("bpm=96\n[score]\nM1/12 X1/12"));
    QVERIFY(!song.has_value());
    const QString why = song.error();
    QVERIFY2(song.error().contains(QStringLiteral("第 3 行")), qPrintable(why));
    QVERIFY2(song.error().contains(QStringLiteral("X1/12")), qPrintable(why));
}

void TestScore::pitch_prints_as_notation() {
    QCOMPARE_PITCH((Pitch{Register::Mid, 4, false}), "M4");
    QCOMPARE_PITCH((Pitch{Register::Mid, 4, true}), "M4#");
    QCOMPARE_PITCH((Pitch{Register::Top, 1, true}), "T1#");
    QCOMPARE_PITCH((Pitch{Register::Low, 7, false}), "L7");
}

void TestScore::empty_score_is_an_error() {
    QVERIFY(!parseScore(QString()).has_value());
    QVERIFY(!parseScore(QStringLiteral("# 只有注释\n")).has_value());
    // 只有元信息、没有音符
    QVERIFY(!parseScore(QStringLiteral("title=空\nbpm=90\n")).has_value());
}

QTEST_GUILESS_MAIN(TestScore)
#include "tst_score.moc"
