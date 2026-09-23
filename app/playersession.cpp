#include "playersession.h"

#include <variant>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>

#include "config.h"
#include "input.h"
#include "player.h"
#include "score.h"

namespace harmonica::app {

namespace {
/// 起播前倒数几秒，给玩家把窗口切回游戏的时间。倒数期间随时可以取消
constexpr int COUNTDOWN_SECS = 3;
}  // namespace

PlayerSession::PlayerSession(Layout layout, QObject* parent)
    : QObject{parent}, layout_{std::move(layout)} {
    countdownTimer_.setInterval(1000);
    pollTimer_.setInterval(100);
    QObject::connect(&countdownTimer_, &QTimer::timeout, this, &PlayerSession::onCountdownTick);
    QObject::connect(&pollTimer_, &QTimer::timeout, this, &PlayerSession::onPollTick);
}

PlayerSession::~PlayerSession() { stopAndJoin(); }

bool PlayerSession::start(const QString& file, bool dryRun) {
    if (file.isEmpty()) {
        lastError_ = QStringLiteral("还没有选中曲子");
        emit statusChanged();
        return false;
    }
    // 谱子路径由程序决定，不由调用方决定
    if (file.contains(QLatin1Char('/')) || file.contains(QLatin1Char('\\'))
        || file.contains(QLatin1Char(':')) || file.contains(QStringLiteral(".."))) {
        lastError_ = QStringLiteral("不是合法的曲谱文件名：%1").arg(file);
        emit statusChanged();
        return false;
    }

    QFile scoreFile{QDir{layout_.dhsDir}.filePath(file)};
    if (!scoreFile.open(QIODevice::ReadOnly)) {
        lastError_ = QStringLiteral("读取 %1 失败：%2").arg(scoreFile.fileName(),
                                                            scoreFile.errorString());
        emit statusChanged();
        return false;
    }
    auto song = parseScore(QString::fromUtf8(scoreFile.readAll()));
    if (!song) {
        lastError_ = song.error();
        emit statusChanged();
        return false;
    }

    // 每次起播都重读一遍配置 —— 改完键位和时序不用重启程序
    auto config = Config::load(layout_.configFile);
    if (!config) {
        lastError_ = config.error();
        emit statusChanged();
        return false;
    }

    actions_ = buildActions(*song, *config);
    config_ = *config;
    calibration_ = false;
    calibrationSteps_.clear();
    calibrationStarts_.clear();

    const QString title = song->title.isEmpty() ? QFileInfo{file}.completeBaseName() : song->title;
    beginRun(file, title, song->durationMs(), dryRun);
    return true;
}

bool PlayerSession::startCalibration() {
    auto config = Config::load(layout_.configFile);
    if (!config) {
        lastError_ = config.error();
        emit statusChanged();
        return false;
    }

    const Song song = harmonica::calibrationSong();
    actions_ = buildActions(song, *config);
    config_ = *config;

    // 音位的按下时刻取自动作表里的第 K 个 KeyDown —— 每个音恰好产生一个 KeyDown，
    // 所以这里不用再算一遍时间。两边对不上说明 core 改岔了，宁可不跑也别瞎指。
    std::vector<double> starts;
    for (const TimedAction& action : actions_) {
        if (std::holds_alternative<KeyDown>(action.action)) starts.push_back(action.atMs);
    }
    const std::vector<CalibrationStep> steps = harmonica::calibrationSteps(*config);
    if (starts.size() != steps.size()) {
        lastError_ = QStringLiteral("校准音位和动作表对不上");
        emit statusChanged();
        return false;
    }

    calibrationStarts_ = starts;
    QVariantList list;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        QStringList buttons;
        for (const MouseButton button : steps[i].buttons) buttons << buttonName(button);
        list.push_back(QVariantMap{
            {QStringLiteral("index"), static_cast<qlonglong>(i)},
            {QStringLiteral("group"), steps[i].group},
            {QStringLiteral("degree"), static_cast<int>(steps[i].degree)},
            {QStringLiteral("key"), keyName(steps[i].vk)},
            {QStringLiteral("buttons"), buttons},
            {QStringLiteral("startMs"), starts[i]},
        });
    }
    calibrationSteps_ = list;
    calibration_ = true;

    beginRun(QString(), song.title, song.durationMs(), false);
    return true;
}

void PlayerSession::beginRun(const QString& file, const QString& title, double totalMs,
                             bool dryRun) {
    dryRun_ = dryRun;
    // 先把上一场收干净再起新的：旧线程的 ReleaseGuard 会先跑完，
    // 按键不会被两场交叉着按住。
    stopAndJoin();

    file_ = file;
    title_ = title;
    totalMs_ = totalMs;
    progressMs_ = 0.0;
    lastError_.clear();
    sent_ = 0;
    dropped_ = 0;
    input::resetStats();
    progress_.store(0);
    stopRequest_.store(false);

    if (dryRun) {
        // 干跑不用切回游戏，也就不用倒数
        state_ = State::Playing;
        emit started();
        launchWorker();
    } else {
        state_ = State::Countdown;
        countdownLeft_ = COUNTDOWN_SECS;
        countdownTimer_.start();
    }
    pollTimer_.start();
    emit statusChanged();
}

void PlayerSession::stop() {
    if (state_ == State::Countdown) {
        finish();
        return;
    }
    if (state_ == State::Playing) {
        // 只置标志。线程看到就收工，按键由它的 ReleaseGuard 统一放干净；
        // 这边的 poll 会注意到「线程已经走了」，再报统计、收悬浮窗。
        stopRequest_.store(true);
    }
}

void PlayerSession::stopAndJoin() {
    stopRequest_.store(true);
    countdownTimer_.stop();
    pollTimer_.stop();
    if (worker_.joinable()) worker_.join();
    state_ = State::Idle;
    countdownLeft_ = 0;
}

void PlayerSession::launchWorker() {
    workerDone_.store(false);
    const bool dryRun = dryRun_;
    worker_ = std::thread{[this, actions = std::move(actions_), cfg = config_, dryRun] {
        play(actions, cfg, stopRequest_, dryRun, &progress_);
        workerDone_.store(true);
    }};
}

void PlayerSession::onCountdownTick() {
    countdownLeft_ -= 1;
    if (countdownLeft_ <= 0) {
        countdownTimer_.stop();
        state_ = State::Playing;
        emit started();
        launchWorker();
    }
    emit statusChanged();
}

void PlayerSession::onPollTick() {
    progressMs_ = static_cast<qreal>(progress_.load());
    if (state_ == State::Playing && workerDone_.load()) {
        finish();
        return;
    }
    emit statusChanged();
}

void PlayerSession::finish() {
    const State previous = state_;
    state_ = State::Idle;
    countdownLeft_ = 0;
    countdownTimer_.stop();
    pollTimer_.stop();

    const auto [ok, failed] = input::stats();
    sent_ = static_cast<qlonglong>(ok);
    dropped_ = static_cast<qlonglong>(failed);

    if (worker_.joinable()) worker_.join();
    dryRun_ = false;
    calibration_ = false;

    emit statusChanged();
    if (previous != State::Idle) emit finished();
}

int calibrationStepAt(const std::vector<double>& startMs, double progressMs) {
    // 动作表是按时间排的，所以从前往后扫，最后一个「已经到点」的就是当前音位
    int current = -1;
    for (std::size_t i = 0; i < startMs.size(); ++i) {
        if (startMs[i] > progressMs) break;
        current = static_cast<int>(i);
    }
    return current;
}

}  // namespace harmonica::app
