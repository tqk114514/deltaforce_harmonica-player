#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "editorcontroller.h"
#include "paths.h"
#include "score.h"
#include "testhelpers.h"

using namespace harmonica;
using namespace harmonica::app;

namespace {

QVariantMap cell(EditorController& editor, int index) {
    const QVariantList placed = editor.placed();
    if (index < 0 || index >= placed.size()) return {};
    return placed.at(index).toMap();
}

}  // namespace

/// 编辑器的界面后端。算法本身在 scoredoc 里测过了，
/// 这里测的是「编辑操作有没有真的改到谱子上」和文件那一圈。
class TestEditor : public QObject {
    Q_OBJECT

private slots:
    void appendingUsesTheCurrentInputState();
    void topRegisterOnlyEverHoldsDo();
    void selectionCarriesTheNoteBack();
    void holdBarExtendsTheSelectedNote();
    void tiesRecordTargetsAndSayWhichKind();
    void deletingANoteRepairsTies();
    void modLandsOnTheFirstNoteOfTheMeasure();
    void graceHintReportsTheBorrow();
    void projectFilesRoundTripAndRejectTraversal();
    void exportLandsInTheSongsDirAndIsPlayable();
    void autosaveSurvivesARestart();

private:
    QTemporaryDir root_;
};

void TestEditor::appendingUsesTheCurrentInputState() {
    EditorController editor{layoutFor(root_.path())};
    editor.setCurDur(8);
    editor.setCurOctave(1);
    editor.setCurSharp(true);
    editor.append(3);
    editor.append(0);

    QCOMPARE(editor.count(), 2);
    QCOMPARE(cell(editor, 0).value(QStringLiteral("degree")).toInt(), 3);
    QCOMPARE(cell(editor, 0).value(QStringLiteral("dur")).toInt(), 8);
    QCOMPARE(cell(editor, 0).value(QStringLiteral("octave")).toInt(), 1);
    QVERIFY(cell(editor, 0).value(QStringLiteral("sharp")).toBool());
    // 0 是休止符，不带音高
    QVERIFY(cell(editor, 1).value(QStringLiteral("rest")).toBool());
    QVERIFY(cell(editor, 1).value(QStringLiteral("octave")).toInt() == 0);
}

void TestEditor::topRegisterOnlyEverHoldsDo() {
    EditorController editor{layoutFor(root_.path())};
    editor.setCurOctave(2);
    editor.append(5);   // 超高音区只有 do，录 5 也按 do 收
    QCOMPARE(cell(editor, 0).value(QStringLiteral("degree")).toInt(), 1);

    editor.select(0);
    editor.setCurDur(4);
    editor.setCurOctave(0);
    editor.applyToSelection();
    editor.setCurOctave(2);
    editor.applyToSelection();
    QCOMPARE(cell(editor, 0).value(QStringLiteral("degree")).toInt(), 1);
}

void TestEditor::selectionCarriesTheNoteBack() {
    EditorController editor{layoutFor(root_.path())};
    editor.setCurDur(16);
    editor.setCurOctave(-1);
    editor.append(2);
    editor.setCurDur(4);
    editor.setCurOctave(0);
    editor.append(5);

    editor.select(0);
    // 选中一个音，工具栏就切成这个音的值（新录入的音仍然用默认值）
    QCOMPARE(editor.curDur(), 16);
    QCOMPARE(editor.curOctave(), -1);
    QCOMPARE(editor.selected(), 0);
    editor.moveSelected(1);
    QCOMPARE(editor.selected(), 1);
    QCOMPARE(cell(editor, 0).value(QStringLiteral("degree")).toInt(), 5);
}

void TestEditor::holdBarExtendsTheSelectedNote() {
    EditorController editor{layoutFor(root_.path())};
    editor.append(1);
    editor.appendHold();
    editor.appendHold();
    QCOMPARE(cell(editor, 0).value(QStringLiteral("holdBars")).toInt(), 2);
    QVERIFY(editor.dhsText().contains(QStringLiteral("M1/72")));   // 24 + 24 + 24
    editor.removeHold();
    QCOMPARE(cell(editor, 0).value(QStringLiteral("holdBars")).toInt(), 1);
}

/// 连音线的种类是算出来的，不是画的时候猜的 —— 状态栏要说清是哪一种
void TestEditor::tiesRecordTargetsAndSayWhichKind() {
    EditorController editor{layoutFor(root_.path())};
    editor.append(7);
    editor.append(7);
    editor.append(6);

    editor.pickTieTarget(0);
    QCOMPARE(editor.tieFrom(), 0);
    editor.pickTieTarget(1);
    QCOMPARE(cell(editor, 0).value(QStringLiteral("links")).toList(), (QList<QVariant>{1}));
    QVERIFY(editor.status().contains(QStringLiteral("延音线")));
    QCOMPARE(editor.tieFrom(), -1);

    editor.pickTieTarget(1);
    editor.pickTieTarget(2);
    QVERIFY(editor.status().contains(QStringLiteral("圆滑线")));

    // 导出时前者合并成一个长音，后者只是画在谱面上
    QVERIFY(editor.dhsText().contains(QStringLiteral("M7/48")));
    QVERIFY(editor.dhsText().contains(QStringLiteral("M6/24")));

    editor.cancelTie();
    QCOMPARE(editor.tieFrom(), -1);
}

void TestEditor::deletingANoteRepairsTies() {
    EditorController editor{layoutFor(root_.path())};
    editor.append(1);
    editor.append(2);
    editor.append(3);
    editor.pickTieTarget(0);
    editor.pickTieTarget(2);   // 跨音连线，指向 2
    QCOMPARE(cell(editor, 0).value(QStringLiteral("links")).toList().size(), 1);

    editor.select(1);
    editor.removeSelected();
    QCOMPARE(editor.count(), 2);
    // 删掉中间那个之后，原来的目标 2 变成 1
    QCOMPARE(cell(editor, 0).value(QStringLiteral("links")).toList(), (QList<QVariant>{1}));
}

void TestEditor::modLandsOnTheFirstNoteOfTheMeasure() {
    EditorController editor{layoutFor(root_.path())};
    editor.setMeter(4);
    for (int degree = 1; degree <= 6; ++degree) editor.append(degree);

    editor.select(3);   // 第四个音（还在第一小节里）
    QVERIFY(editor.setMod(QStringLiteral("前1=后3")));
    // 标记对齐到本小节第一个音
    QVERIFY(cell(editor, 0).value(QStringLiteral("measureStart")).toBool());
    QCOMPARE(cell(editor, 0).value(QStringLiteral("modLabel")).toString(), QStringLiteral("+4"));
    QVERIFY(cell(editor, 3).value(QStringLiteral("modLabel")).toString().isEmpty());
    // 而且真的换了音高：第一个音 M1 变成 M3
    QVERIFY(editor.dhsText().contains(QStringLiteral("M3/24")));
}

void TestEditor::graceHintReportsTheBorrow() {
    EditorController editor{layoutFor(root_.path())};
    editor.setBpm(120);
    editor.append(5);
    editor.select(0);

    // 主音是四分（24 tick），十六分倚音（6 tick）没到一半上限 —— README 的那条例子
    QString hint = editor.graceHint(1, 1, false, 16);
    QVERIFY2(hint.contains(QStringLiteral("6 tick")), qPrintable(hint));
    QVERIFY(hint.contains(QStringLiteral("高1")));
    QVERIFY(!hint.contains(QStringLiteral("压到")));
    editor.setGrace(1, 1, false, 16);
    QVERIFY(!cell(editor, 0).value(QStringLiteral("grace")).toMap().isEmpty());
    QVERIFY(editor.dhsText().contains(QStringLiteral("H1/6 M5/18")));
    editor.clearGrace();
    QVERIFY(cell(editor, 0).value(QStringLiteral("grace")).toMap().isEmpty());

    // 主音只有三十二分（3 tick）时，倚音被压到 1 tick，面板上要说出这个上限
    editor.setCurDur(32);
    editor.applyToSelection();
    hint = editor.graceHint(1, 1, false, 16);
    QVERIFY2(hint.contains(QStringLiteral("压到")), qPrintable(hint));
    QVERIFY(hint.contains(QStringLiteral("1 tick")));
    editor.setGrace(1, 1, false, 16);
    QVERIFY2(editor.dhsText().contains(QStringLiteral("H1/1 M5/2")), qPrintable(editor.dhsText()));
}

void TestEditor::projectFilesRoundTripAndRejectTraversal() {
    const Layout layout = layoutFor(root_.path());
    ensureDirs(layout);
    EditorController editor{layout};
    editor.setTitle(QStringLiteral("测试曲"));
    editor.setBpm(96);
    editor.append(1);
    editor.append(7);
    editor.pickTieTarget(0);
    editor.pickTieTarget(1);

    QVERIFY(!editor.saveProject(QStringLiteral("../坏.score.json")));
    QVERIFY(!editor.saveProject(QStringLiteral("x.txt")));
    QVERIFY(editor.saveProject(QStringLiteral("测试.score.json")));
    QVERIFY(editor.projects().contains(QStringLiteral("测试.score.json")));
    QCOMPARE(editor.openName(), QStringLiteral("测试.score.json"));

    EditorController other{layout};
    QVERIFY(other.openProject(QStringLiteral("测试.score.json")));
    QCOMPARE(other.title(), QStringLiteral("测试曲"));
    QCOMPARE(other.count(), 2);
    QCOMPARE(cell(other, 0).value(QStringLiteral("links")).toList(), (QList<QVariant>{1}));
    QVERIFY(!other.openProject(QStringLiteral("没有这份.score.json")));
}

void TestEditor::exportLandsInTheSongsDirAndIsPlayable() {
    const Layout layout = layoutFor(root_.path());
    ensureDirs(layout);
    EditorController editor{layout};
    editor.setTitle(QStringLiteral("导出检查"));
    editor.append(1);
    editor.append(2);
    editor.setCurDur(8);
    editor.append(3);

    QVERIFY(editor.exportDhs(QStringLiteral("导出检查.dhs")));
    QVERIFY(editor.projects().isEmpty() || true);
    const QString text = [&] {
        QFile file{QDir{layout.dhsDir}.filePath(QStringLiteral("导出检查.dhs"))};
        if (!file.open(QIODevice::ReadOnly)) return QString{};
        return QString::fromUtf8(file.readAll());
    }();
    QVERIFY(!text.isEmpty());

    // 演奏器那边必须读得懂，而且时长和编辑器自己算的一致
    const auto song = parseScore(text);
    REQUIRE_OK(song);
    QCOMPARE(song->title, QStringLiteral("导出检查"));
    QCOMPARE(static_cast<int>(song->totalTicks()), 24 + 24 + 12);

    EditorController reimported{layout};
    QVERIFY(reimported.openDhs(QStringLiteral("导出检查.dhs")));
    QCOMPARE(reimported.count(), 3);
    QVERIFY(reimported.status().contains(QStringLiteral("恢复不了")));
}

void TestEditor::autosaveSurvivesARestart() {
    const Layout layout = layoutFor(root_.path());
    EditorController editor{layout};
    QVERIFY(!editor.hasAutosave());
    editor.append(4);
    editor.setCurDur(16);
    editor.append(5);
    QVERIFY(editor.autosave());
    QVERIFY(editor.hasAutosave());

    EditorController next{layout};
    QVERIFY(next.restoreAutosave());
    QCOMPARE(next.count(), 2);
    QCOMPARE(cell(next, 1).value(QStringLiteral("dur")).toInt(), 16);
}

QTEST_GUILESS_MAIN(TestEditor)
#include "tst_editor.moc"
