#pragma once

//! F9 开始 / 重播，F10 立即停止。
//!
//! 全局热键是演奏时唯一靠得住的控制方式 —— 那时候焦点在游戏里，界面上的按钮点不到。
//! Qt 只有应用内快捷键（`QShortcut`），没有全局的，所以这里落到 `RegisterHotKey` 上。

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QString>

class QWindow;

namespace harmonica::app {

/// 快捷键 id。测试要能自己往窗口投一条 `WM_HOTKEY` 来验分发，
/// 所以 id 是公开契约的一部分，不是内部细节。
inline constexpr int F9_HOTKEY_ID = 0xC0DE;
inline constexpr int F10_HOTKEY_ID = 0xC0DF;

class Hotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit Hotkeys(QObject* parent = nullptr);
    ~Hotkeys() override;

    /// 把热键绑到某个窗口的句柄上。窗口藏进托盘之后句柄还在，所以照样收得到。
    /// 返回是否全部注册成功；被别的程序占用的键会各报一句，不影响其余的。
    bool registerTo(QWindow* window);
    void unregister();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    /// "F9" / "F10"
    void activated(const QString& key);

private:
    struct Binding {
        int id = 0;
        unsigned int vk = 0;
        QString name;
    };

    QList<Binding> bindings_;
    unsigned long long hwnd_ = 0;
};

}  // namespace harmonica::app
