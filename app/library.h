#pragma once

//! 曲库扫描：把一个目录里的 `.dhs` 全解析一遍，给出列表要的东西。
//!
//! 时长**必须**由 core 的同一个解析器算 —— 界面上写的时长得和真吹出来的一致，
//! 在界面那边另写一套迟早会对不上。

#include <QString>
#include <QVariantList>

#include <vector>

namespace harmonica::app {

struct SongInfo {
    QString file;      ///< 文件名，如 `鸟之诗.dhs`
    QString title;
    QString artist;
    double bpm = 0.0;
    double playsMs = 0.0;
    std::size_t notes = 0;
    QString error;     ///< 空 = 解析成功
};

/// 扫描演奏谱目录。解析失败的文件也列出来（`error` 非空）——
/// 悄悄跳过只会让人以为「文件没被认出来」而不是「文件是坏的」。
std::vector<SongInfo> scanLibrary(const QString& dhsDir);

/// 给 QML 的形状：字段名用小驼峰，和界面里 `modelData.title` 那种写法一致
QVariantList libraryAsVariantList(const std::vector<SongInfo>& songs);

}  // namespace harmonica::app
