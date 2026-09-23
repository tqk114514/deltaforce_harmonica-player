#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include "palette.h"
#include "paths.h"

class QWindow;

namespace harmonica::app {

/// 窗口标识。标题只在这里定义一份 —— 找另一个实例的窗口是按标题找的，
/// 两边各写一遍迟早会对不上。
inline constexpr const char* MAIN_TITLE = "口琴演奏器";
inline constexpr const char* MAIN_OBJECT = "main";
inline constexpr const char* OVERLAY_OBJECT = "overlay";

/// 给 QML 用的那一个后端对象：环境信息、悬浮窗开关、打开曲谱目录。
///
/// 这里不放任何时序计算 —— 演奏和校准在轮③④ 接进来，
/// 它们只调用 `core/`，自己不算时间。
class Backend : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString mainTitle READ mainTitle CONSTANT)
    Q_PROPERTY(QString baseDir READ baseDir CONSTANT)
    /// 曲库扫的那个目录（`songs/dhs`）
    Q_PROPERTY(QString songsDir READ songsDir CONSTANT)
    Q_PROPERTY(QString configFile READ configFile CONSTANT)
    /// 进程是否以管理员身份运行。游戏若是管理员，这个为 false 时按键一个都进不去。
    Q_PROPERTY(bool elevated READ elevated CONSTANT)
    /// 曲谱目录建不出来时的原因（多半是没写权限），正常是空串
    Q_PROPERTY(QString dirError READ dirError CONSTANT)
    /// 配置文件写不出去时的原因（同样是没写权限）；写不出去时设置改了不会留档
    Q_PROPERTY(QString configError READ configError CONSTANT)
    /// 曲库里选中的那首（按文件名）。F9 演奏的就是它，所以放在后端而不是页面里
    Q_PROPERTY(QString selectedFile READ selectedFile WRITE setSelectedFile NOTIFY selectionChanged)

    Q_PROPERTY(QColor windowBackground READ windowBackground CONSTANT)
    Q_PROPERTY(QColor windowText READ windowText CONSTANT)
    Q_PROPERTY(QColor border READ border CONSTANT)
    Q_PROPERTY(QColor accent READ accent CONSTANT)
    Q_PROPERTY(QColor mutedText READ mutedText CONSTANT)

public:
    Backend(Layout layout, QString dirError, QString configError, QObject* parent = nullptr);

    [[nodiscard]] QString version() const;
    [[nodiscard]] static QString mainTitle() { return QString::fromUtf8(MAIN_TITLE); }
    [[nodiscard]] QString baseDir() const;
    [[nodiscard]] QString songsDir() const;
    [[nodiscard]] QString configFile() const;
    [[nodiscard]] bool elevated() const;
    [[nodiscard]] QString dirError() const { return dirError_; }
    [[nodiscard]] QString configError() const { return configError_; }
    [[nodiscard]] QString selectedFile() const { return selectedFile_; }
    void setSelectedFile(QString file);

    /// 扫演奏谱目录，返回曲库列表（含解析失败的那些）。时长的算法只有 core 那一份
    Q_INVOKABLE QVariantList listSongs() const;

    [[nodiscard]] static QColor windowBackground() { return WINDOW_BACKGROUND; }
    [[nodiscard]] static QColor windowText() { return WINDOW_TEXT; }
    [[nodiscard]] static QColor border() { return BORDER; }
    [[nodiscard]] static QColor accent() { return ACCENT; }
    [[nodiscard]] static QColor mutedText() { return MUTED_TEXT; }

    /// 两个窗口建好之后交给后端：找窗口、叫到最前这些事只在 C++ 这边做一次
    void setOverlayWindow(QWindow* window);
    void setMainWindow(QWindow* window);

    /// 把主窗口从最小化/后台叫到最前并给它焦点。只有托盘用得上 ——
    /// 它不是「演奏」路径，所以这里该激活就激活。
    void showMainWindow();

    Q_INVOKABLE void showOverlay();
    Q_INVOKABLE void hideOverlay();
    Q_INVOKABLE bool openSongsFolder();

signals:
    void selectionChanged();

private:
    Layout layout_;
    QString dirError_;
    QString configError_;
    QString selectedFile_;
    bool elevated_ = false;
    QPointer<QWindow> overlay_;
    QPointer<QWindow> main_;
};

}  // namespace harmonica::app
