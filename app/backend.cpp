#include "backend.h"

#include <QCoreApplication>
#include <QWindow>

#include "win32.h"

namespace harmonica::app {

Backend::Backend(Layout layout, QString dirError, QString configError, QObject* parent)
    : QObject(parent),
      layout_(std::move(layout)),
      dirError_(std::move(dirError)),
      configError_(std::move(configError)),
      elevated_(win32::isElevated()) {}

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

bool Backend::openSongsFolder() { return win32::openPath(layout_.dhsDir); }

}  // namespace harmonica::app
