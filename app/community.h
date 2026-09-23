#pragma once

//! 社区曲库：清单、时长、下载与校验。全部在 C++ 侧。
//!
//! 以前那一版把 `fetch` 和 `crypto.subtle` 放在界面里，是因为 Rust 没有网络和
//! 音频设施。Qt 两样都有，所以收回来：少一层跨语言边界，也不用把文件字节当成
//! JSON 数字数组传一遍。
//!
//! 要守住的结论一条没变：两个文件都核对过才落盘、任一失败都不落半份、
//! 清单里没有 `sha256` 的条目不下载、覆盖本地同名曲目前必须显式确认。

#include <memory>
#include <vector>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include "paths.h"

class QNetworkAccessManager;

namespace harmonica::app {

struct CommunityEntry {
    QString title;
    QString dhsUrl;
    QString nmnUrl;
    QString dhsSha;
    QString nmnSha;
    QString text;        ///< 已经读回来的 .dhs 正文，试听直接用，省一次网络
    double playsMs = 0.0;
    bool hasDuration = false;
    QString error;       ///< 空 = 正常；有值时这一行标红，不许下载
};

/// 校验用的那串摘要：**算的是拿到的原始字节**。
/// 不能先按文本解码再编码回去 —— BOM、换行、编码都可能在中途变样，
/// 那样算出来的是「文本的哈希」而不是「文件的哈希」。
bool sha256Matches(const QByteArray& bytes, const QString& expectedHex);

/// 解析清单（不碰网络）。相对路径以清单地址为基准。
/// 没有 `dhs_URL` 的条目列出来但标着错；没有 `sha256` 的条目标成「不可下载」。
std::vector<CommunityEntry> parseCommunityManifest(const QByteArray& json, const QUrl& base);

QVariantList communityAsVariantList(const std::vector<CommunityEntry>& entries);

class CommunityManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(QVariantList songs READ songs NOTIFY songsChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit CommunityManager(Layout layout, QObject* parent = nullptr);

    /// 拉清单，再把每份 `.dhs` 读进内存算时长（不落盘）。
    /// 时长由 core 的那个解析器算 —— 界面上的时长得和真吹的一致
    Q_INVOKABLE void refresh();

    /// 下载一首：两个文件都拿到、都核对过才交给磁盘。任何一个出问题都不落盘，
    /// 万一写第二个失败会把第一个删掉，尽量不留半份
    Q_INVOKABLE void download(const QString& title);

    /// 本地已经有同名曲目？界面用它在下载前先问一句
    Q_INVOKABLE bool hasLocal(const QString& title) const;

    /// 已经读回来的谱子正文，交给 Preview 去发声
    Q_INVOKABLE QString textOf(const QString& title) const;

    [[nodiscard]] QVariantList songs() const;
    [[nodiscard]] bool isBusy() const { return busy_ > 0; }
    [[nodiscard]] QString status() const { return status_; }

signals:
    void songsChanged();
    void busyChanged();
    void statusChanged();
    /// 一句结论进界面，具体原因（哪个文件、期望/实际摘要）进控制台
    void downloadFailed(QString title, QString reason);
    void downloadDone(QString title);

private:
    void setStatus(QString text);
    void get(const QUrl& url, std::function<void(QByteArray, QString)> done);

    Layout layout_;
    QNetworkAccessManager* network_ = nullptr;
    std::vector<CommunityEntry> entries_;
    int busy_ = 0;
    QString status_;
};

}  // namespace harmonica::app
