#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "config.h"
#include "paths.h"

using namespace harmonica;
using namespace harmonica::app;

class TestPaths : public QObject {
    Q_OBJECT

private slots:
    void layoutKeepsEverythingUnderRoot();
    void ensureDirsCreatesAllThree();
    void ensureDirsSaysWhyItFailed();
    void writesDefaultConfigWhenMissing();
    void neverOverwritesAnExistingConfig();
};

void TestPaths::layoutKeepsEverythingUnderRoot() {
    const Layout layout = layoutFor(QStringLiteral("D:/Games/Harmonica"));
    QCOMPARE(layout.songsDir, QStringLiteral("D:/Games/Harmonica/songs"));
    QCOMPARE(layout.dhsDir, QStringLiteral("D:/Games/Harmonica/songs/dhs"));
    QCOMPARE(layout.notationDir,
             QStringLiteral("D:/Games/Harmonica/songs/numbered_musical_notation"));
    QCOMPARE(layout.configFile, QStringLiteral("D:/Games/Harmonica/harmonica.ini"));
}

void TestPaths::ensureDirsCreatesAllThree() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const Layout layout = layoutFor(root.path());

    QCOMPARE(ensureDirs(layout), QString());
    for (const QString& dir : {layout.songsDir, layout.dhsDir, layout.notationDir}) {
        QVERIFY2(QFileInfo::exists(dir), qPrintable(dir));
    }
    // 第二次启动不能再报错
    QCOMPARE(ensureDirs(layout), QString());
}

/// 装在 Program Files 下面时非管理员进程没有写权限 —— 那不算致命错误，
/// 但要给出具体原因让界面去说。
void TestPaths::ensureDirsSaysWhyItFailed() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString blockedFile = QDir{root.path()}.filePath(QStringLiteral("afile"));
    QFile file{blockedFile};
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("x");
    file.close();

    const QString reason = ensureDirs(layoutFor(blockedFile));
    QVERIFY2(!reason.isEmpty(), "在一个普通文件下面建目录必须失败");
    // 原因里要带上是哪个目录，界面才有东西可显示
    const QString shown = reason;
    QVERIFY2(reason.contains(blockedFile), qPrintable(shown));
}

void TestPaths::writesDefaultConfigWhenMissing() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const Layout layout = layoutFor(root.path());

    QCOMPARE(ensureConfigFile(layout), QString());
    QVERIFY(QFileInfo::exists(layout.configFile));

    const QString text = [&layout] {
        QFile file{layout.configFile};
        if (!file.open(QIODevice::ReadOnly)) return QString{};
        return QString::fromUtf8(file.readAll());
    }();
    QVERIFY2(!text.isEmpty(), "配置文件没写出去");
    // 第一次写出去的就是 core 的默认值：不许有「自己的默认值」
    QVERIFY2(text.contains(QStringLiteral("mouse_lead_ms=60")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("note_gap_ms=50")), qPrintable(text));
    QVERIFY(text.contains(QStringLiteral("# 修饰键提前量")));

    const auto loaded = Config::load(layout.configFile);
    QVERIFY2(loaded.has_value(), "配置要能读回来");
    QCOMPARE(loaded->timing.mouseLeadMs, 60);
    QCOMPARE(loaded->timing.noteGapMs, 50);
}

/// 用户改过的配置绝不能被启动流程盖掉
void TestPaths::neverOverwritesAnExistingConfig() {
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const Layout layout = layoutFor(root.path());
    QFile file{layout.configFile};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QString custom = QStringLiteral("[timing]\nmouse_lead_ms=123\n");
    file.write(custom.toUtf8());
    file.close();

    QCOMPARE(ensureConfigFile(layout), QString());

    QFile again{layout.configFile};
    QVERIFY(again.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(again.readAll()), custom);

    const auto loaded = Config::load(layout.configFile);
    QVERIFY2(loaded.has_value(), "配置要能读回来");
    QCOMPARE(loaded->timing.mouseLeadMs, 123);
}

QTEST_GUILESS_MAIN(TestPaths)
#include "tst_paths.moc"
