#include <algorithm>
#include <optional>

#include <QtTest>

#include "config.h"
#include "score.h"
#include "testhelpers.h"

using namespace harmonica;

namespace {

/// 一个音位实际会按下去的东西，压成一行字方便比对：`left+middle+Z`
QString resolveLabel(const Config& cfg, Register register_, int degree, bool sharp) {
    const auto [key, buttons] = cfg.resolve(Pitch{register_, static_cast<std::uint8_t>(degree), sharp});
    QStringList parts;
    for (const MouseButton button : buttons) parts << buttonName(button);
    parts << keyName(key);
    return parts.join(QLatin1Char('+'));
}

/// 只用于测试：解析失败是测试用例本身写错了，直接断言炸掉
Config withIni(const QString& text) {
    Config cfg;
    const auto applied = cfg.applyIni(text);
    Q_ASSERT_X(applied.has_value(), "withIni",
               qUtf8Printable(applied.has_value() ? QString() : applied.error()));
    return cfg;
}

/// Config 不可打印，逐字段比，失败信息才有可读性
void expectSame(const Config& actual, const Config& want) {
    for (int i = 0; i < 7; ++i) QCOMPARE(actual.degreeKeys[i], want.degreeKeys[i]);
    QCOMPARE(actual.highDoKey, want.highDoKey);
    QCOMPARE(actual.lowButton.has_value(), want.lowButton.has_value());
    QCOMPARE(actual.highButton.has_value(), want.highButton.has_value());
    QCOMPARE(actual.sharpButton.has_value(), want.sharpButton.has_value());
    if (actual.lowButton && want.lowButton) QVERIFY(*actual.lowButton == *want.lowButton);
    if (actual.highButton && want.highButton) QVERIFY(*actual.highButton == *want.highButton);
    if (actual.sharpButton && want.sharpButton) QVERIFY(*actual.sharpButton == *want.sharpButton);
    QCOMPARE(actual.timing.mouseLeadMs, want.timing.mouseLeadMs);
    QCOMPARE(actual.timing.noteGapMs, want.timing.noteGapMs);
}

}  // namespace

class TestConfig : public QObject {
    Q_OBJECT

private slots:
    void defaultKeysAreZxcvbnm();
    void resolveMatrixMatchesTheNotation();
    void topDoUsesTheEighthKey();
    void noneModifierDropsThatRegister();
    void iniOverrides();
    void iniRoundTrip();
    void defaultIniTextLoadsBackAsDefault();
    void highDoKeySurvivesRoundTrip();
    void removedTimingKeysAreIgnored();
    void badValuesAreReported();
    void keyNamesRoundTrip();
    void loadMissingFileGivesDefaults();
    void allKeysDeduplicates();
    void toIniIsDocumented();
};

void TestConfig::defaultKeysAreZxcvbnm() {
    const Config cfg;
    QCOMPARE(keyName(cfg.degreeKeys[0]), QStringLiteral("Z"));
    QCOMPARE(keyName(cfg.degreeKeys[6]), QStringLiteral("M"));
    QCOMPARE(keyName(cfg.highDoKey), QStringLiteral("COMMA"));
    QCOMPARE(cfg.timing.mouseLeadMs, 60);
    // 实测 5ms 时游戏里连续同音会粘成一个（重新按下识别不到），50 起步才分得开
    QCOMPARE(cfg.timing.noteGapMs, 50);
}

/// 音区/升号 -> 鼠标修饰，和 README 那张对照表一致
void TestConfig::resolveMatrixMatchesTheNotation() {
    const Config cfg;
    QCOMPARE(resolveLabel(cfg, Register::Mid, 1, false), QStringLiteral("Z"));
    QCOMPARE(resolveLabel(cfg, Register::Mid, 1, true), QStringLiteral("middle+Z"));
    QCOMPARE(resolveLabel(cfg, Register::Low, 3, false), QStringLiteral("left+C"));
    QCOMPARE(resolveLabel(cfg, Register::Low, 3, true), QStringLiteral("left+middle+C"));
    // 高音区一律右键 + 度数键（H1 也是右键 + Z，不用第 8 键）
    QCOMPARE(resolveLabel(cfg, Register::High, 1, false), QStringLiteral("right+Z"));
    QCOMPARE(resolveLabel(cfg, Register::High, 2, true), QStringLiteral("right+middle+X"));
    // 中音区不带升号时不按任何鼠标键
    QCOMPARE(cfg.buttonsFor(Register::Mid, false).size(), std::size_t{0});
    // 每个音位的键都落在度数键表上，不会撞到别的键
    QCOMPARE(resolveLabel(cfg, Register::Mid, 7, false), QStringLiteral("M"));
}

void TestConfig::topDoUsesTheEighthKey() {
    const Config cfg;
    const auto [key, buttons] = cfg.resolve(Pitch{Register::Top, 1, true});
    QCOMPARE(keyName(key), QStringLiteral("COMMA"));
    // 超高音 do = 高音区修饰（右键）+ 第 8 键，再加半音叠中键
    QCOMPARE(buttons.size(), std::size_t{2});
    QVERIFY(std::find(buttons.begin(), buttons.end(), MouseButton::Right) != buttons.end());
    QVERIFY(std::find(buttons.begin(), buttons.end(), MouseButton::Middle) != buttons.end());
    // 超高音区不带升号时只有右键
    QCOMPARE(cfg.buttonsFor(Register::Top, false).size(), std::size_t{1});
}

/// 某一档设成 none 的代价：那一档音区会按中音区发出来，音高不对 —— 但键位仍然要能用
void TestConfig::noneModifierDropsThatRegister() {
    const Config cfg = withIni(QStringLiteral("[octave]\nlow=none\n"));
    QVERIFY(!cfg.lowButton.has_value());
    QCOMPARE(resolveLabel(cfg, Register::Low, 1, false), QStringLiteral("Z"));
    QCOMPARE(resolveLabel(cfg, Register::High, 1, false), QStringLiteral("right+Z"));
    // 低音 + 半音：只剩中键，左键那档已经关掉
    QCOMPARE(resolveLabel(cfg, Register::Low, 1, true), QStringLiteral("middle+Z"));
}

void TestConfig::iniOverrides() {
    const Config cfg = withIni(QStringLiteral(
        "[keys]\nd3=K\n[octave]\nlow=none\n[timing]\nmouse_lead_ms=20\nnote_gap_ms=55\n"));
    QCOMPARE(keyName(cfg.degreeKeys[2]), QStringLiteral("K"));
    QVERIFY(!cfg.lowButton.has_value());
    QCOMPARE(cfg.timing.mouseLeadMs, 20);
    QCOMPARE(cfg.timing.noteGapMs, 55);
    // 没改到的项保持默认
    QCOMPARE(keyName(cfg.degreeKeys[0]), QStringLiteral("Z"));
}

/// `toIni` 和 `applyIni` 必须是一对：写出去再读回来，得是同一个配置。
/// 设置界面「保存」就是写 toIni 的结果，这条断了，用户的设置就会悄悄丢。
void TestConfig::iniRoundTrip() {
    Config cfg;
    cfg.degreeKeys = {vk('A'), vk('S'), vk('D'), vk('F'), vk('G'), vk('H'), vk('J')};
    cfg.highDoKey = 0xBE;  // PERIOD
    cfg.lowButton = std::nullopt;
    cfg.highButton = MouseButton::Left;
    cfg.sharpButton = MouseButton::Middle;
    cfg.timing.mouseLeadMs = 75;
    cfg.timing.noteGapMs = 12;

    Config back;
    QVERIFY(back.applyIni(cfg.toIni()).has_value());
    expectSame(back, cfg);
}

/// 默认配置文件读回来必须是默认配置 —— 否则第一次启动就写了一份「自己的默认值」。
void TestConfig::defaultIniTextLoadsBackAsDefault() {
    Config back;
    QVERIFY(back.applyIni(Config::defaultIniText()).has_value());
    expectSame(back, Config{});
}

/// 逗号键（第 8 键）是特殊的一个，单独验一下 round-trip 里的名字转换
void TestConfig::highDoKeySurvivesRoundTrip() {
    Config back;
    QVERIFY(back.applyIni(Config{}.toIni()).has_value());
    QCOMPARE(back.highDoKey, HIGH_DO_KEY);
    QCOMPARE(keyName(back.highDoKey), QStringLiteral("COMMA"));
}

/// 已删除的保底项还留在老配置文件里时，必须被安静忽略而不是报错
void TestConfig::removedTimingKeysAreIgnored() {
    const Config cfg = withIni(QStringLiteral(
        "[timing]\nmin_hold_ms=90\nsame_note_ratio=0.667\nmodifier_release_lead_ms=5\n"
        "min_key_hold_ms=1\nenforce_min_hold=true\n"));
    QCOMPARE(cfg.timing.mouseLeadMs, Config{}.timing.mouseLeadMs);
    QCOMPARE(cfg.timing.noteGapMs, Config{}.timing.noteGapMs);
}

/// 认得的项写错了要报错：静默吞掉只会让用户以为设置生效了
void TestConfig::badValuesAreReported() {
    QVERIFY(!Config{}.applyIni(QStringLiteral("[keys]\nd1=@")).has_value());
    QVERIFY(!Config{}.applyIni(QStringLiteral("[octave]\nlow=up")).has_value());
    QVERIFY(!Config{}.applyIni(QStringLiteral("[timing]\nmouse_lead_ms=abc")).has_value());
    QVERIFY(!Config{}.applyIni(QStringLiteral("[timing]\nnote_gap_ms=-5")).has_value());
    // 但完全陌生的项不报错（老配置文件的残留）
    QVERIFY(Config{}.applyIni(QStringLiteral("[nope]\nwhatever=1\n")).has_value());
    // 报错要带行号和原值，不然手改文件的人找不到是哪行
    const auto failed = Config{}.applyIni(QStringLiteral("[keys]\nd1=Z\nd2=@@\n"));
    QVERIFY(!failed.has_value());
    const QString why = failed.error();
    QVERIFY2(failed.error().contains(QStringLiteral("第 3 行")), qPrintable(why));
}

/// 界面上认的键名和文件里认的完全一样：keyName 和 parseKeyName 互逆
void TestConfig::keyNamesRoundTrip() {
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        const auto parsed = parseKeyName(QString::fromLatin1(&letter, 1));
        QVERIFY2(parsed.has_value(), "单个字母必须能当键名");
        QCOMPARE(*parsed, vk(letter));
        QCOMPARE(keyName(*parsed), QString::fromLatin1(&letter, 1));
    }
    for (char digit = '0'; digit <= '9'; ++digit) {
        const auto parsed = parseKeyName(QString::fromLatin1(&digit, 1));
        QVERIFY2(parsed.has_value(), "单个数字必须能当键名");
        QCOMPARE(keyName(*parsed), QString::fromLatin1(&digit, 1));
    }
    for (const char* name : {"COMMA", "PERIOD", "SEMICOLON", "SLASH", "SPACE", "LBRACKET",
                             "RBRACKET", "MINUS", "EQUAL"}) {
        const auto parsed = parseKeyName(QString::fromLatin1(name));
        QVERIFY2(parsed.has_value(), name);
        QCOMPARE(keyName(*parsed), QString::fromLatin1(name));
    }
    // 符号写法也认，且和规范名指向同一个键；大小写无所谓
    QCOMPARE(*parseKeyName(QStringLiteral(",")), *parseKeyName(QStringLiteral("COMMA")));
    QCOMPARE(*parseKeyName(QStringLiteral("comma")), *parseKeyName(QStringLiteral("COMMA")));
    QVERIFY(!parseKeyName(QStringLiteral("F13")).has_value());
    QVERIFY(!parseKeyName(QStringLiteral("")).has_value());
    QVERIFY(!parseKeyName(QStringLiteral("AB")).has_value());

    QVERIFY(*parseButton(QStringLiteral("left")) == MouseButton::Left);
    QVERIFY(*parseButton(QStringLiteral("MIDDLE")) == MouseButton::Middle);
    QVERIFY(*parseButton(QStringLiteral("right")) == MouseButton::Right);
    QVERIFY(!parseButton(QStringLiteral("none"))->has_value());
    QVERIFY(!parseButton(QStringLiteral("wheel")).has_value());
}

void TestConfig::loadMissingFileGivesDefaults() {
    const auto loaded = Config::load(QStringLiteral("Z:/definitely/not/here/harmonica.ini"));
    REQUIRE_OK(loaded);
    expectSame(*loaded, Config{});
}

/// 第 8 键换成了度数键之一时，释放表里不许出现两份
void TestConfig::allKeysDeduplicates() {
    Config cfg;
    QCOMPARE(cfg.allKeys().size(), std::size_t{8});
    cfg.highDoKey = vk('Z');
    QCOMPARE(cfg.allKeys().size(), std::size_t{7});
    // 必须先存进局部量：`allKeys().begin()` 和 `allKeys().end()` 是两次调用，
    // 拿到的是两个不同 vector 的迭代器，debug 版直接炸断言
    const auto keys = cfg.allKeys();
    QVERIFY(std::is_sorted(keys.begin(), keys.end()));
}

/// 第一次运行写出去的文件要自带说明，不用翻 README
void TestConfig::toIniIsDocumented() {
    const QString text = Config::defaultIniText();
    QVERIFY(text.contains(QStringLiteral("# 三角洲行动 口琴自动演奏 - 键位配置")));
    QVERIFY(text.contains(QStringLiteral("d8=COMMA")));
    QVERIFY(text.contains(QStringLiteral("mouse_lead_ms=60")));
    QVERIFY(text.contains(QStringLiteral("note_gap_ms=50")));
    QVERIFY(text.endsWith(QStringLiteral("note_gap_ms=50\n")));
    // 行尾只用 LF：CRLF 会让仓库里那份和线上那份的 sha256 完全不同
    QVERIFY(!text.contains(QStringLiteral("\r")));
    // 每一项都要能被读回来，注释行不许长得像配置行
    Config back;
    QVERIFY(back.applyIni(text).has_value());
    expectSame(back, Config{});
}

QTEST_GUILESS_MAIN(TestConfig)
#include "tst_config.moc"
