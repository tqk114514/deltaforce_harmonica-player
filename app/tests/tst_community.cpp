#include <QCryptographicHash>
#include <QtTest>

#include "community.h"

using namespace harmonica::app;

namespace {
const QUrl BASE{QStringLiteral("https://harmonica-songs.pages.dev/manifest.json")};
}

/// 社区曲库里唯一能被离线测的就是这两件事：摘要算法的入口，和清单的解析规则。
/// 网络那半边留给真实调用 —— 在这里造假响应只会让测试看着绿、线上照样错。
class TestCommunity : public QObject {
    Q_OBJECT

private slots:
    void digestIsOverRawBytes();
    void encodingChangeBreaksTheDigest();
    void manifestResolvesPathsAndLowercasesSha();
    void manifestMarksWhatCannotBeDownloaded();
    void manifestShapeIsChecked();
};

void TestCommunity::digestIsOverRawBytes() {
    // SHA-256("abc") 的公开值
    const char* expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    QVERIFY(sha256Matches(QByteArray("abc"), QString::fromLatin1(expected)));
    // 清单里写大写也得认，比较前统一成小写
    QVERIFY(sha256Matches(QByteArray("abc"), QString::fromLatin1(expected).toUpper()));
    QVERIFY(!sha256Matches(QByteArray("abd"), QString::fromLatin1(expected)));
    // 没有摘要 = 不下载，别拿空串蒙过去
    QVERIFY(!sha256Matches(QByteArray("abc"), QString()));
}

/// 为什么必须按字节算而不是按文本算：BOM 一变，摘要就完全两样
void TestCommunity::encodingChangeBreaksTheDigest() {
    const QByteArray plain = QByteArrayLiteral("title=x\n");
    const QByteArray withBom = QByteArray("\xEF\xBB\xBF") + plain;
    QVERIFY(plain != withBom);

    const QString bomDigest = QString::fromLatin1(
        QCryptographicHash::hash(withBom, QCryptographicHash::Sha256).toHex());
    QVERIFY(sha256Matches(withBom, bomDigest));
    QVERIFY2(!sha256Matches(plain, bomDigest),
             "按文本再编码回去，算出来的是「文本的哈希」而不是「文件的哈希」");
}

void TestCommunity::manifestResolvesPathsAndLowercasesSha() {
    const QByteArray json = R"JSON([
      {"song_title":"鸟之诗","dhs_URL":"dhs/鸟之诗.dhs","dhs_sha256":"AA11",
       "nmn_URL":"numbered_musical_notation/鸟之诗.score.json","nmn_sha256":"BB22"},
      {"song_title":"千本樱","dhs_URL":"dhs/千本樱.dhs","dhs_sha256":"cc33",
       "nmn_URL":"dhs/../x.json","nmn_sha256":"dd44"}
    ])JSON";

    const auto entries = parseCommunityManifest(json, BASE);
    QCOMPARE(entries.size(), std::size_t{2});
    // 列表按标题排序，别靠下标认人
    const auto bird = std::find_if(entries.begin(), entries.end(), [](const CommunityEntry& e) {
        return e.title == QStringLiteral("鸟之诗");
    });
    QVERIFY(bird != entries.end());
    // 相对路径以清单地址为基准
    QVERIFY(bird->dhsUrl.startsWith(QStringLiteral("https://harmonica-songs.pages.dev/dhs/")));
    QVERIFY(bird->dhsUrl.endsWith(QStringLiteral(".dhs")));
    QVERIFY(bird->nmnUrl.startsWith(
        QStringLiteral("https://harmonica-songs.pages.dev/numbered_musical_notation/")));
    // 摘要统一小写比较
    QCOMPARE(bird->dhsSha, QStringLiteral("aa11"));
    QVERIFY(bird->error.isEmpty());
}

/// 缺 `sha256` 的条目不下载；缺地址的条目连列出来都要说清为什么
void TestCommunity::manifestMarksWhatCannotBeDownloaded() {
    const QByteArray json = R"JSON([
      {"song_title":"没摘要","dhs_URL":"dhs/a.dhs","nmn_URL":"n/a.json"},
      {"song_title":"没地址","dhs_sha256":"aa","nmn_sha256":"bb"},
      {"song_title":"","dhs_URL":"dhs/x.dhs","dhs_sha256":"aa","nmn_sha256":"bb"}
    ])JSON";

    const auto entries = parseCommunityManifest(json, BASE);
    // 没有标题又没有错误的条目不列（第三条），所以只剩两行
    QCOMPARE(entries.size(), std::size_t{2});
    // 列表按标题排过序，别靠下标认人
    auto findByTitle = [&entries](const QString& title) {
        for (const CommunityEntry& entry : entries) {
            if (entry.title == title) return entry;
        }
        return CommunityEntry{};
    };
    QVERIFY(findByTitle(QStringLiteral("没摘要")).error.contains(QStringLiteral("sha256")));
    QVERIFY(findByTitle(QStringLiteral("没地址")).error.contains(QStringLiteral("dhs_URL")));
}

void TestCommunity::manifestShapeIsChecked() {
    const auto notArray = parseCommunityManifest(QByteArrayLiteral("{\"song_title\":\"x\"}"), BASE);
    QCOMPARE(notArray.size(), std::size_t{1});
    QVERIFY(notArray[0].error.contains(QStringLiteral("不是数组")));

    const auto garbage = parseCommunityManifest(QByteArrayLiteral("<html>"), BASE);
    QVERIFY(!garbage.empty());
    QVERIFY(!garbage[0].error.isEmpty());
}

QTEST_GUILESS_MAIN(TestCommunity)
#include "tst_community.moc"
