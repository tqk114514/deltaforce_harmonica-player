#include "backend.h"

#include <QCoreApplication>
#include <QFile>
#include <QWindow>

#include "config.h"
#include "library.h"
#include "win32.h"

namespace harmonica::app {
namespace {

/// 设置界面允许填的时序上限（毫秒）。配置文件里手写不受这个限制，
/// 这里拦的是「多打一个零」这类输入 —— 提前两秒按修饰键显然不是本意。
constexpr int TIMING_MAX_MS = 2000;

QVariantMap keyConfigOf(const Config& cfg) {
    QStringList degrees;
    for (const std::uint16_t key : cfg.degreeKeys) degrees << keyName(key);

    const auto button = [](std::optional<MouseButton> b) {
        return b ? buttonName(*b) : QStringLiteral("none");
    };
    return {
        {QStringLiteral("degrees"), degrees},
        {QStringLiteral("highDo"), keyName(cfg.highDoKey)},
        {QStringLiteral("low"), button(cfg.lowButton)},
        {QStringLiteral("high"), button(cfg.highButton)},
        {QStringLiteral("sharp"), button(cfg.sharpButton)},
        {QStringLiteral("mouseLeadMs"), cfg.timing.mouseLeadMs},
        {QStringLiteral("noteGapMs"), cfg.timing.noteGapMs},
    };
}

}  // namespace

Backend::Backend(Layout layout, QString dirError, QString configError, QObject* parent)
    : QObject(parent),
      layout_(std::move(layout)),
      dirError_(std::move(dirError)),
      configError_(std::move(configError)),
      elevated_(win32::isElevated()) {
    reloadKeyConfig();
}

void Backend::reloadKeyConfig() {
    auto cfg = Config::load(layout_.configFile);
    if (!cfg) {
        // 配置文件读不回来时界面显示默认值，但把原因留在 settingsError 里 ——
        // 文件坏了必须说一声，不然用户以为改生效了
        settingsError_ = cfg.error();
        keyConfig_ = keyConfigOf(Config{});
    } else {
        keyConfig_ = keyConfigOf(*cfg);
    }
    emit keyConfigChanged();
}

bool Backend::saveKeyConfig(const QVariantMap& values) {
    const auto fail = [this](const QString& why) {
        settingsError_ = why;
        emit keyConfigChanged();
        return false;
    };

    const QStringList degrees = values.value(QStringLiteral("degrees")).toStringList();
    if (degrees.size() != 7) {
        return fail(QStringLiteral("度数键需要 7 个，收到 %1 个").arg(degrees.size()));
    }

    bool leadOk = false;
    bool gapOk = false;
    const int lead = values.value(QStringLiteral("mouseLeadMs")).toInt(&leadOk);
    const int gap = values.value(QStringLiteral("noteGapMs")).toInt(&gapOk);
    if (!leadOk || !gapOk || lead < 0 || gap < 0 || lead > TIMING_MAX_MS || gap > TIMING_MAX_MS) {
        return fail(QStringLiteral("时序参数得是 0 到 %1 之间的毫秒数").arg(TIMING_MAX_MS));
    }

    // 改之前先要能读回来：读不回来就别盖掉用户的东西
    auto current = Config::load(layout_.configFile);
    if (!current) return fail(current.error());
    Config cfg = *current;

    for (int i = 0; i < 7; ++i) {
        auto parsed = parseKeyName(degrees[i]);
        if (!parsed) {
            return fail(QStringLiteral("度数 %1 的键名：%2").arg(i + 1).arg(parsed.error()));
        }
        cfg.degreeKeys[i] = *parsed;
    }
    {
        auto parsed = parseKeyName(values.value(QStringLiteral("highDo")).toString());
        if (!parsed) return fail(QStringLiteral("第 8 键的键名：%1").arg(parsed.error()));
        cfg.highDoKey = *parsed;
    }
    const auto applyButton = [&values, &fail, &cfg](const char* name,
                                                    std::optional<MouseButton> Config::*field) {
        auto parsed = parseButton(values.value(QString::fromLatin1(name)).toString());
        if (!parsed) {
            fail(QStringLiteral("%1 那一档：%2").arg(QString::fromLatin1(name), parsed.error()));
            return false;
        }
        cfg.*field = *parsed;
        return true;
    };
    if (!applyButton("low", &Config::lowButton) || !applyButton("high", &Config::highButton)
        || !applyButton("sharp", &Config::sharpButton)) {
        return false;
    }
    cfg.timing.mouseLeadMs = lead;
    cfg.timing.noteGapMs = gap;

    QFile file{layout_.configFile};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(QStringLiteral("写入配置失败：%1").arg(file.errorString()));
    }
    file.write(cfg.toIni().toUtf8());
    file.close();

    // 保存之后一律以磁盘上那份为准刷新界面 —— 写出去和读回来必须一模一样，
    // 这条不变量由 core 的测试盯着
    settingsError_.clear();
    reloadKeyConfig();
    return true;
}

QString Backend::version() const { return QCoreApplication::applicationVersion(); }

QString Backend::baseDir() const { return layout_.root; }

QString Backend::songsDir() const { return layout_.dhsDir; }

QString Backend::configFile() const { return layout_.configFile; }

bool Backend::elevated() const { return elevated_; }

void Backend::setOverlayWindow(QWindow* window) { overlay_ = window; }

void Backend::setMainWindow(QWindow* window) { main_ = window; }

void Backend::showOverlay() {
    if (overlay_ == nullptr) return;
    overlay_->show();
    // 置顶，但不激活 —— 激活就会把焦点从游戏抢走，后面的音一个都进不去
    win32::pinTopmostWithoutActivating(overlay_);
}

void Backend::hideOverlay() {
    if (overlay_ != nullptr) overlay_->hide();
}

void Backend::showMainWindow() {
    if (main_ == nullptr) return;
    main_->show();
    main_->raise();
    win32::bringToForeground(main_);
}

QVariantList Backend::listSongs() const { return libraryAsVariantList(scanLibrary(layout_.dhsDir)); }

void Backend::setSelectedFile(QString file) {
    if (file == selectedFile_) return;
    selectedFile_ = std::move(file);
    emit selectionChanged();
}

bool Backend::openSongsFolder() { return win32::openPath(layout_.dhsDir); }

}  // namespace harmonica::app
