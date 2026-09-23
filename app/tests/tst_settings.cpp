#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "config.h"
#include "paths.h"

using namespace harmonica;
using namespace harmonica::app;

namespace {

void writeText(const QString& path, const QString& text) {
    QFile file{path};
    Q_ASSERT(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(text.toUtf8());
    file.close();
}

QString readText(const QString& path) {
    QFile file{path};
    Q_ASSERT(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
}

QVariantMap settings(const QStringList& degrees, const QString& highDo, int lead, int gap) {
    return {
        {QStringLiteral("degrees"), degrees},
        {QStringLiteral("highDo"), highDo},
        {QStringLiteral("low"), QStringLiteral("left")},
        {QStringLiteral("high"), QStringLiteral("right")},
        {QStringLiteral("sharp"), QStringLiteral("middle")},
        {QStringLiteral("mouseLeadMs"), lead},
        {QStringLiteral("noteGapMs"), gap},
    };
}

}  // namespace

/// 设置页的存与取。规则只有一份：校验走 core 的 `parseKeyName` / `parseButton`，
/// 写回走 core 的 `toIni` —— 界面上填不进去的，配置文件里也写不出来。
class TestSettings : public QObject {
    Q_OBJECT

private slots:
    void showsWhatTheFileSays();
    void savingWritesTheFileAndReadsBack();
    void invalidKeyNameIsRejectedAndLeavesFileAlone();
    void timingOutsideTheSaneRangeIsRejected();
    void degreesMustBeSeven();

private:
    QTemporaryDir root_;
};

void TestSettings::showsWhatTheFileSays() {
    QVERIFY(root_.isValid());
    const Layout layout = layoutFor(root_.path());
    writeText(layout.configFile,
              QStringLiteral("[keys]\nd3=K\n[timing]\nmouse_lead_ms=25\nnote_gap_ms=40\n"));

    Backend backend{layout, {}, {}};
    const QVariantMap shown = backend.keyConfig();
    QCOMPARE(shown.value(QStringLiteral("mouseLeadMs")).toInt(), 25);
    QCOMPARE(shown.value(QStringLiteral("noteGapMs")).toInt(), 40);
    QCOMPARE(shown.value(QStringLiteral("degrees")).toStringList().at(2), QStringLiteral("K"));
    // 没改到的项保持默认，并且名字是配置文件里那种写法
    QCOMPARE(shown.value(QStringLiteral("degrees")).toStringList().at(0), QStringLiteral("Z"));
    QCOMPARE(shown.value(QStringLiteral("highDo")).toString(), QStringLiteral("COMMA"));
    QCOMPARE(shown.value(QStringLiteral("low")).toString(), QStringLiteral("left"));
    QVERIFY(backend.settingsError().isEmpty());
}

void TestSettings::savingWritesTheFileAndReadsBack() {
    QVERIFY(root_.isValid());
    const Layout layout = layoutFor(root_.path());
    QVERIFY(ensureConfigFile(layout).isEmpty());
    Backend backend{layout, {}, {}};

    const QStringList degrees{QStringLiteral("A"), QStringLiteral("S"), QStringLiteral("D"),
                              QStringLiteral("F"), QStringLiteral("G"), QStringLiteral("H"),
                              QStringLiteral("J")};
    QVERIFY(backend.saveKeyConfig(settings(degrees, QStringLiteral("PERIOD"), 80, 35)));
    QVERIFY(backend.settingsError().isEmpty());

    // 保存之后界面显示的必须是磁盘上那份
    QCOMPARE(backend.keyConfig().value(QStringLiteral("degrees")).toStringList(), degrees);
    QCOMPARE(backend.keyConfig().value(QStringLiteral("highDo")).toString(),
             QStringLiteral("PERIOD"));
    QCOMPARE(backend.keyConfig().value(QStringLiteral("mouseLeadMs")).toInt(), 80);

    const auto loaded = Config::load(layout.configFile);
    QVERIFY(loaded.has_value());
    QCOMPARE(keyName(loaded->degreeKeys[6]), QStringLiteral("J"));
    QCOMPARE(loaded->highDoKey, *parseKeyName(QStringLiteral("PERIOD")));
    QCOMPARE(loaded->timing.mouseLeadMs, 80);
    QCOMPARE(loaded->timing.noteGapMs, 35);
    // 带注释的模板不能被保存弄没
    QVERIFY(readText(layout.configFile).contains(QStringLiteral("# 修饰键提前量")));
}

void TestSettings::invalidKeyNameIsRejectedAndLeavesFileAlone() {
    QVERIFY(root_.isValid());
    const Layout layout = layoutFor(root_.path());
    ensureConfigFile(layout);
    Backend backend{layout, {}, {}};
    const QString before = readText(layout.configFile);

    QStringList degrees(7, QStringLiteral("Z"));
    degrees[0] = QStringLiteral("@");
    QVERIFY(!backend.saveKeyConfig(settings(degrees, QStringLiteral("COMMA"), 60, 50)));
    QVERIFY2(backend.settingsError().contains(QStringLiteral("无法识别的按键名")),
             qPrintable(backend.settingsError()));
    QCOMPARE(readText(layout.configFile), before);
}

void TestSettings::timingOutsideTheSaneRangeIsRejected() {
    QVERIFY(root_.isValid());
    const Layout layout = layoutFor(root_.path());
    ensureConfigFile(layout);
    Backend backend{layout, {}, {}};
    const QStringList degrees{QStringLiteral("Z"), QStringLiteral("X"), QStringLiteral("C"),
                              QStringLiteral("V"), QStringLiteral("B"), QStringLiteral("N"),
                              QStringLiteral("M")};

    // 多打一个零这种输入，拦下来比照抄进文件好
    QVERIFY(!backend.saveKeyConfig(settings(degrees, QStringLiteral("COMMA"), 900000, 50)));
    QVERIFY(!backend.settingsError().isEmpty());
    QVERIFY(!backend.saveKeyConfig(settings(degrees, QStringLiteral("COMMA"), 60, -5)));
    // 报过的错不能赖着不走：一次正常保存要把它清掉
    QVERIFY(backend.saveKeyConfig(settings(degrees, QStringLiteral("COMMA"), 60, 50)));
    QVERIFY(backend.settingsError().isEmpty());
    QCOMPARE(Config::load(layout.configFile)->timing.noteGapMs, 50);
}

void TestSettings::degreesMustBeSeven() {
    QVERIFY(root_.isValid());
    const Layout layout = layoutFor(root_.path());
    ensureConfigFile(layout);
    Backend backend{layout, {}, {}};

    QVERIFY(!backend.saveKeyConfig(settings(QStringList{QStringLiteral("Z")},
                                            QStringLiteral("COMMA"), 60, 50)));
    QVERIFY2(backend.settingsError().contains(QStringLiteral("需要 7 个")),
             qPrintable(backend.settingsError()));
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
