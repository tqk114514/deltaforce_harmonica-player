//! 入口：单实例、目录约定、两个窗口的 Win32 属性、托盘。
//!
//! 界面全在 QML 里（`app/qml/`），时序与键位全在 `core/` 里。
//! 这一层不许出现第三份规则。

#include <QApplication>
#include <QDebug>
#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QPalette>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QSystemTrayIcon>
#include <QWindow>
#include <qqml.h>

#include "backend.h"
#include "config.h"
#include "hotkeys.h"
#include "input.h"
#include "palette.h"
#include "paths.h"
#include "playersession.h"
#include "win32.h"

#ifndef HARMONICA_VERSION
#define HARMONICA_VERSION "0.0.0"
#endif

namespace harmonica::app {
namespace {

/// 关掉主窗口 = 收进托盘，不是退出。
/// 不拦的话窗口会被真的销毁，而进程还活着（悬浮窗那个窗口还在），
/// 于是托盘上的「显示主窗口」就再也没窗口可以显示了。
class HideOnClose final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::Close) return false;
        event->setAccepted(false);
        if (auto* window = qobject_cast<QWindow*>(watched)) window->hide();
        return true;
    }
};

/// 退出前把所有按键放掉 —— 和 F10、托盘「退出」一样，这也是一条退出路径。
/// 鼠标键卡在按下状态会让整个系统点不动，宁可多发几个空松开也不要漏。
void releaseAllKeys(const QString& configFile) {
    const Config cfg = Config::load(configFile).value_or(Config{});
    input::releaseAll(cfg.allKeys());
}

void buildTray(QApplication& application, Backend& backend, const QIcon& icon) {
    auto* tray = new QSystemTrayIcon{icon, &application};
    tray->setToolTip(QString::fromUtf8(MAIN_TITLE));

    auto* menu = new QMenu{};
    menu->addAction(QObject::tr("显示主窗口"), [&backend] { backend.showMainWindow(); });
    menu->addSeparator();
    menu->addAction(QObject::tr("退出"), [&application, &backend] {
        releaseAllKeys(backend.configFile());
        application.quit();
    });
    tray->setContextMenu(menu);

    // 左键不弹菜单，直接叫出主窗口：这是托盘程序的一般习惯
    QObject::connect(tray, &QSystemTrayIcon::activated, &backend,
                     [&backend](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger
                             || reason == QSystemTrayIcon::DoubleClick) {
                             backend.showMainWindow();
                         }
                     });
    tray->show();
}

}  // namespace

int run(int argc, char** argv) {
    QApplication application{argc, argv};
    application.setApplicationName(QStringLiteral("harmonica-player"));
    application.setApplicationVersion(QStringLiteral(HARMONICA_VERSION));
    // 关掉所有窗口不等于退出：主窗口收进托盘，悬浮窗也是隐藏的
    application.setQuitOnLastWindowClosed(false);

    // 控件不用 Windows 原生样式：它会把浅色的输入框画在深色底上，对高度也有自己的
    // 主张（启动日志里那串 "implicit height is smaller than minimum height" 就是它报的）。
    // 界面整体是自配的深色，所以用 Fusion，颜色还是从 palette.h 那一份取。
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    QPalette dark;
    dark.setColor(QPalette::Window, WINDOW_BACKGROUND);
    dark.setColor(QPalette::WindowText, WINDOW_TEXT);
    dark.setColor(QPalette::Base, SURFACE);
    dark.setColor(QPalette::Text, WINDOW_TEXT);
    dark.setColor(QPalette::Button, SURFACE);
    dark.setColor(QPalette::ButtonText, WINDOW_TEXT);
    dark.setColor(QPalette::Highlight, ACCENT);
    dark.setColor(QPalette::HighlightedText, WINDOW_BACKGROUND);
    dark.setColor(QPalette::ToolTipBase, SURFACE);
    dark.setColor(QPalette::ToolTipText, WINDOW_TEXT);
    application.setPalette(dark);

    // 抢不到锁说明已经有一个在跑 —— 把它的窗口叫到前面，自己安静退出
    if (!win32::takeSingleInstanceLock()) {
        win32::focusRunningInstance(QString::fromUtf8(MAIN_TITLE));
        return 0;
    }

    const Layout layout = layoutFor(baseDir());
    const QString dirError = ensureDirs(layout);
    const QString configError = ensureConfigFile(layout);

    Backend backend{layout, dirError, configError};
    qmlRegisterSingletonInstance("Harmonica", 1, 0, "Backend", &backend);

    PlayerSession session{layout};
    qmlRegisterSingletonInstance("Harmonica", 1, 0, "Session", &session);
    // 一场开始就把悬浮窗叫出来，结束（自然吹完 / 被停 / 倒数被取消）就收掉
    QObject::connect(&session, &PlayerSession::started, &backend, [&backend] { backend.showOverlay(); });
    QObject::connect(&session, &PlayerSession::finished, &backend,
                     [&backend] { backend.hideOverlay(); });

    // 图标由 make_icons.py 生成、编进资源（见 app/CMakeLists.txt）；
    // 窗口图标和托盘图标用同一个 QIcon，不再准备第二份
    const QIcon icon{QStringLiteral(":/icons/icon.ico")};
    application.setWindowIcon(icon);

    QQmlApplicationEngine engine;
    engine.loadFromModule("Harmonica", "Main");
    engine.loadFromModule("Harmonica", "Overlay");
    if (engine.rootObjects().isEmpty()) return -1;

    bool foundMain = false;
    QWindow* mainWindow = nullptr;
    for (QObject* object : engine.rootObjects()) {
        auto* window = qobject_cast<QQuickWindow*>(object);
        if (window == nullptr) continue;

        if (window->objectName() == QLatin1String(OVERLAY_OBJECT)) {
            backend.setOverlayWindow(window);
            // 悬浮窗的两个关键性质必须在它被显示出来之前就打好：晚了的话第一次显示
            // 就会抢一次焦点，刚好把演奏开头几个音吃掉。
            win32::makeNonActivating(window);
            window->hide();
        } else if (window->objectName() == QLatin1String(MAIN_OBJECT)) {
            foundMain = true;
            mainWindow = window;
            backend.setMainWindow(window);
            window->installEventFilter(new HideOnClose{window});
            // 系统原生标题栏，只染颜色：按钮、拖拽、贴边、双击最大化全都保持原生行为
            win32::tintCaption(window);
        }
    }

    // 主窗口没建出来时进程还活着、但屏幕上什么都没有 —— 那是最难查的一种「没反应」。
    // QML 加载失败必须在这里就变成退出码，别靠人盯 stderr。
    if (!foundMain) {
        qCritical("找不到主窗口（objectName=\"%s\"）—— QML 没加载成功", MAIN_OBJECT);
        return -1;
    }

    // 热键绑在主窗口的句柄上：窗口收进托盘之后句柄仍然存在，所以照样收得到。
    // 注册失败（被别的程序占了）只在控制台报一句，界面上的按钮照样能点。
    auto* hotkeys = new Hotkeys{&application};
    application.installNativeEventFilter(hotkeys);
    hotkeys->registerTo(mainWindow);
    QObject::connect(hotkeys, &Hotkeys::activated, &session, [&session, &backend](const QString& key) {
        if (key == QLatin1String("F10")) {
            session.stop();
            return;
        }
        session.start(backend.selectedFile(), false);
    });

    buildTray(application, backend, icon);
    return application.exec();
}

}  // namespace harmonica::app

int main(int argc, char** argv) { return harmonica::app::run(argc, argv); }
