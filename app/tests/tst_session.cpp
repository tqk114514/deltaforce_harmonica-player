#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "playersession.h"

using namespace harmonica::app;

namespace {

/// 4 个音、bpm 1200 → 整场约 0.26 秒，测试等得起，又足够长到能被中途停掉。
/// 元信息必须在 [score] 之前 —— 写在后面就会被当成音符 token，解析器会拒掉它。
constexpr const char* SCORE =
    "bpm=1200\nticks_per_beat=12\n"
    "[score]\n"
    "M1/12 M1/12 H1/12 M1/12\n";

}  // namespace

class TestPlayerSession : public QObject {
    Q_OBJECT

private slots:
    void init();
    void dryRunSendsNoInputButReportsProgress();
    void stopDuringCountdownCancelsWithoutPlaying();
    void badFileNamesAreRejected();
    void replayReplacesTheRunningSession();

private:
    QTemporaryDir root_;
    std::unique_ptr<PlayerSession> session_;
};

void TestPlayerSession::init() {
    if (session_) return;
    Q_ASSERT(root_.isValid());
    // 谱子必须放进布局里那个 `songs/dhs` —— 演奏器只认那一个目录
    const Layout layout = layoutFor(root_.path());
    Q_ASSERT(QDir{}.mkpath(layout.dhsDir));
    QFile file{QDir{layout.dhsDir}.filePath(QStringLiteral("测试.dhs"))};
    Q_ASSERT(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(SCORE));
    file.close();
    session_ = std::make_unique<PlayerSession>(layout);
}

/// 干跑走同一条时间轴，但一个输入都不发 —— 这条测试同时保证它不会往桌面上按键
void TestPlayerSession::dryRunSendsNoInputButReportsProgress() {
    QSignalSpy finished{session_.get(), &PlayerSession::finished};
    QVERIFY(session_->start(QStringLiteral("测试.dhs"), true));
    QVERIFY(session_->isPlaying());
    QVERIFY(finished.wait(5000));

    QCOMPARE(session_->sent(), qlonglong{0});
    QCOMPARE(session_->dropped(), qlonglong{0});
    QVERIFY2(session_->progressMs() > 0.0, "干跑也要报进度，悬浮窗才有东西显示");
    QCOMPARE(session_->title(), QStringLiteral("未命名"));
    QVERIFY(!session_->isPlaying());
}

/// 倒数是给玩家回游戏的时间，这期间按停止等于取消这一场，一个键都不该按
void TestPlayerSession::stopDuringCountdownCancelsWithoutPlaying() {
    QSignalSpy finished{session_.get(), &PlayerSession::finished};
    QVERIFY(session_->start(QStringLiteral("测试.dhs"), false));
    QVERIFY(session_->isCountingDown());
    QCOMPARE(session_->countdownSecs(), 3);

    session_->stop();
    QCOMPARE(finished.count(), 1);
    QVERIFY(!session_->isPlaying());
    QVERIFY(!session_->isCountingDown());
    QCOMPARE(session_->progressMs(), 0.0);
}

void TestPlayerSession::badFileNamesAreRejected() {
    QVERIFY(!session_->start(QString(), false));
    QVERIFY2(session_->lastError() == QStringLiteral("还没有选中曲子"),
             qPrintable(session_->lastError()));

    QVERIFY(!session_->start(QStringLiteral("../escape.dhs"), false));
    QVERIFY(!session_->start(QStringLiteral("a\\b.dhs"), false));
    QVERIFY(!session_->start(QStringLiteral("C:/x.dhs"), false));
    QVERIFY(!session_->start(QStringLiteral("没有这首.dhs"), false));
    QVERIFY(!session_->lastError().isEmpty());
    QVERIFY(!session_->isPlaying());
}

/// F9 的语义是「开始 / 重播」：旧的一场必须先收干净（按键由它的 ReleaseGuard 放掉），
/// 再起新的一场 —— 两场交叉着按键会留下卡住的鼠标键。
void TestPlayerSession::replayReplacesTheRunningSession() {
    QSignalSpy finished{session_.get(), &PlayerSession::finished};
    QVERIFY(session_->start(QStringLiteral("测试.dhs"), true));
    QVERIFY(session_->start(QStringLiteral("测试.dhs"), true));
    QCOMPARE(finished.count(), 0);  // 旧的被静默收掉，不算一场结束

    QVERIFY(finished.wait(5000));
    QCOMPARE(finished.count(), 1);
    QVERIFY(!session_->isPlaying());
}

QTEST_GUILESS_MAIN(TestPlayerSession)
#include "tst_session.moc"
