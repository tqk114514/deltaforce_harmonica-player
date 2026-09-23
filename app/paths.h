#pragma once

//! 程序目录约定：配置和曲谱只认 exe 旁边那一份。
//!
//! 纯逻辑（除了基目录取自 exe 路径），所以能整体被测试盯住 —— 装到哪、
//! 读写到哪，全在这里决定，界面层不许自己拼路径。

#include <QString>

namespace harmonica::app {

/// 演奏谱目录（曲库扫这里）
inline constexpr const char* DHS_SUBDIR = "dhs";
/// 编辑器工程目录（`.score.json`）
inline constexpr const char* NOTATION_SUBDIR = "numbered_musical_notation";

/// 一个程序根目录对应的全部路径
struct Layout {
    QString root;
    QString songsDir;
    QString dhsDir;
    QString notationDir;
    QString configFile;
};

/// exe 所在目录。不往上找 —— 免得上层目录碰巧有同名文件就读到别处去。
QString baseDir();

Layout layoutFor(const QString& root);

/// 保证曲谱目录存在，用户不必先手工建目录。
/// 全部就绪返回空串；否则返回第一个失败的原因（多半是装在 Program Files 下面没写权限，
/// 那不算致命错误 —— 界面显示「目录不可用」和原因，程序照常启动）。
QString ensureDirs(const Layout& layout);

/// 保证配置文件在：没有就写一份**带注释的**默认配置出去，
/// 这样第一次打开程序的人能直接看到有哪些项可以调。
/// 已存在的文件绝不覆盖 —— 那是用户改过的东西。
QString ensureConfigFile(const Layout& layout);

}  // namespace harmonica::app
