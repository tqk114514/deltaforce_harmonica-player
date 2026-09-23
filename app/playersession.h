#pragma once

//! 演奏链路：起线程跑 core 的动作表、把「停」传进去、把进度和结局报给界面。
//!
//! 这里**不计算任何时间** —— 动作表、绝对时间轴、修饰键的先后全在 `core/`。
//! 本类只做三件事：把谱子交给 `core::play`、置停止标志、每 100ms 读一次
//! 演奏线程里那颗原子数当作进度。倒计时是界面 chrome（给玩家回游戏的时间），
//! 用 QTimer 数秒，不碰动作表。

#include <atomic>
#include <thread>
#include <vector>

#include <QObject>
#include <QString>
#include <QTimer>

#include "config.h"
#include "paths.h"
#include "player.h"

namespace harmonica::app {

class PlayerSession : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool playing READ isPlaying NOTIFY statusChanged)
    /// 起播前的倒数（3 秒）。倒数中途可以取消
    Q_PROPERTY(bool countingDown READ isCountingDown NOTIFY statusChanged)
    Q_PROPERTY(int countdownSecs READ countdownSecs NOTIFY statusChanged)
    Q_PROPERTY(QString title READ title NOTIFY statusChanged)
    Q_PROPERTY(QString file READ file NOTIFY statusChanged)
    /// 干跑：走同一条时间轴，但不发送任何输入
    Q_PROPERTY(bool dryRun READ isDryRun NOTIFY statusChanged)
    Q_PROPERTY(qreal totalMs READ totalMs NOTIFY statusChanged)
    Q_PROPERTY(qreal progressMs READ progressMs NOTIFY statusChanged)
    /// 悬浮窗要显示的「还剩多久」。就是总长减进度，不在这里发明第二套时间
    Q_PROPERTY(qreal remainingMs READ remainingMs NOTIFY statusChanged)
    /// 上一场结束时 core 的发送统计。「N 个被系统丢弃」= 权限问题
    Q_PROPERTY(qlonglong sent READ sent NOTIFY statusChanged)
    Q_PROPERTY(qlonglong dropped READ dropped NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)

public:
    explicit PlayerSession(Layout layout, QObject* parent = nullptr);
    /// 析构一定等线程收工 —— 收工路径会松开所有按键（core 的 `ReleaseGuard`）
    ~PlayerSession() override;

    /// 开始演奏。已经有一场在跑就先停掉再起（F9 的语义就是「开始 / 重播」）。
    /// 失败时返回 false 并把原因写进 `lastError`。
    Q_INVOKABLE bool start(const QString& file, bool dryRun = false);

    /// 立即停止。倒数中的话等于取消这一场
    Q_INVOKABLE void stop();

    [[nodiscard]] bool isPlaying() const { return state_ == State::Playing; }
    [[nodiscard]] bool isCountingDown() const { return state_ == State::Countdown; }
    [[nodiscard]] int countdownSecs() const { return countdownLeft_; }
    [[nodiscard]] QString title() const { return title_; }
    [[nodiscard]] QString file() const { return file_; }
    [[nodiscard]] bool isDryRun() const { return dryRun_; }
    [[nodiscard]] qreal totalMs() const { return totalMs_; }
    [[nodiscard]] qreal progressMs() const { return progressMs_; }
    [[nodiscard]] qreal remainingMs() const { return totalMs_ - progressMs_; }
    [[nodiscard]] qlonglong sent() const { return sent_; }
    [[nodiscard]] qlonglong dropped() const { return dropped_; }
    [[nodiscard]] QString lastError() const { return lastError_; }

signals:
    void statusChanged();
    /// 一场真的开始了（倒数走完了）
    void started();
    /// 一场结束了：自然吹完、被停掉、或倒数被取消
    void finished();

private:
    enum class State { Idle, Countdown, Playing };

    void stopAndJoin();
    void launchWorker();
    void onCountdownTick();
    void onPollTick();
    void finish();

    Layout layout_;
    QTimer countdownTimer_;
    QTimer pollTimer_;

    /// 只给 GUI 线程读写；工作线程只碰下面三个原子量
    State state_ = State::Idle;
    int countdownLeft_ = 0;
    bool dryRun_ = false;
    QString title_;
    QString file_;
    qreal totalMs_ = 0.0;
    qreal progressMs_ = 0.0;
    qlonglong sent_ = 0;
    qlonglong dropped_ = 0;
    QString lastError_;

    std::thread worker_;
    /// 起播前算好的动作表和当时的配置；交给线程之后就整个搬过去（线程期间不读主线程状态）
    std::vector<TimedAction> actions_;
    Config config_;
    std::atomic<bool> stopRequest_{false};
    std::atomic<bool> workerDone_{true};
    std::atomic<std::uint64_t> progress_{0};
};

}  // namespace harmonica::app
