#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "library.h"
#include "score.h"

using namespace harmonica;
using namespace harmonica::app;

namespace {

void writeScore(const QString& dir, const QString& name, const QString& text) {
    QFile file{QDir{dir}.filePath(name)};
    Q_ASSERT(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.toUtf8());
    file.close();
}

}  // namespace

class TestLibrary : public QObject {
    Q_OBJECT

private slots:
    void listsSortedAndCarriesDurations();
    void brokenFilesAreListedWithTheirError();
    void missingDirIsEmpty();
    void bomPrefixedFileStillParses();
};

void TestLibrary::listsSortedAndCarriesDurations() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeScore(root.path(), QStringLiteral("b 第二首.dhs"),
               QStringLiteral("title=第二首\nbpm=120\nticks_per_beat=12\n[score]\nM1/12 M2/12"));
    writeScore(root.path(), QStringLiteral("a 第一首.dhs"),
               QStringLiteral("title=第一首\nbpm=120\nticks_per_beat=12\n[score]\nM1/12 M1/12 M1/24"));

    const auto songs = scanLibrary(root.path());
    QCOMPARE(songs.size(), std::size_t{2});
    // 按文件名排序，界面顺序才稳定
    QCOMPARE(songs[0].file, QStringLiteral("a 第一首.dhs"));
    QCOMPARE(songs[1].file, QStringLiteral("b 第二首.dhs"));
    QCOMPARE(songs[0].title, QStringLiteral("第一首"));
    QCOMPARE(songs[0].notes, std::size_t{3});
    QVERIFY(songs[0].error.isEmpty());

    // 时长必须由 core 的同一个解析器算出来：两边对不上就是界面另写了一套
    const auto direct = parseScore(QStringLiteral(
        "title=第一首\nbpm=120\nticks_per_beat=12\n[score]\nM1/12 M1/12 M1/24"));
    QVERIFY(direct.has_value());
    QCOMPARE(songs[0].playsMs, direct->durationMs());
    QVERIFY(qAbs(songs[0].playsMs - 2000.0) < 0.01);

    const QVariantList asList = libraryAsVariantList(songs);
    QCOMPARE(asList.size(), 2);
    QCOMPARE(asList[0].toMap().value(QStringLiteral("file")).toString(),
             QStringLiteral("a 第一首.dhs"));
}

/// 解析失败的文件也列出来（界面标红），而不是悄悄跳过 —— 跳过只会让人以为文件没被扫到
void TestLibrary::brokenFilesAreListedWithTheirError() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    writeScore(root.path(), QStringLiteral("坏的.dhs"), QStringLiteral("title=x\n[score]\nQ1/12"));
    writeScore(root.path(), QStringLiteral("好的.dhs"), QStringLiteral("[score]\nM1/12"));

    const auto songs = scanLibrary(root.path());
    QCOMPARE(songs.size(), std::size_t{2});
    QCOMPARE(songs[0].file, QStringLiteral("坏的.dhs"));
    QVERIFY(!songs[0].error.isEmpty());
    QVERIFY(songs[0].title.isEmpty());
    QVERIFY(songs[1].error.isEmpty());
}

void TestLibrary::missingDirIsEmpty() {
    QVERIFY(scanLibrary(QStringLiteral("Z:/definitely/not/here")).empty());
}

void TestLibrary::bomPrefixedFileStillParses() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QFile file{QDir{root.path()}.filePath(QStringLiteral("带BOM.dhs"))};
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\xEF\xBB\xBF");
    file.write(QStringLiteral("title=带BOM\nbpm=100\n[score]\nM1/12").toUtf8());
    file.close();

    const auto songs = scanLibrary(root.path());
    QCOMPARE(songs.size(), std::size_t{1});
    QVERIFY2(songs[0].error.isEmpty(), qPrintable(songs[0].error));
    QCOMPARE(songs[0].title, QStringLiteral("带BOM"));
}

QTEST_GUILESS_MAIN(TestLibrary)
#include "tst_library.moc"
