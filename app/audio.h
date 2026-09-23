#pragma once

//! 试听：把一份 `.dhs` 合成成一段能听的声音。
//!
//! 这段声音**不是演奏**。演奏只负责按键，真正的声音是游戏里的口琴发的；
//! 试听是离线预览。但**时值和音高都只有一份实现**：谱子交给 core 解析、
//! `preview_notes` 折算成「从曲首算起的时间 + 相对中音区 do 的半音数」，
//! 这里只负责把半音数变成频率、排进音频时钟。界面不许自己数 tick。
//!
//! 以前放在浏览器里是因为 Rust 没有音频设施；Qt 有 `QAudioSink`，就收回来了。

#include <QIODevice>
#include <QObject>

#include "preview.h"
#include "score.h"

class QAudioSink;

namespace harmonica::app {

/// 一段按笔记好的 PCM 源。`readData` 里按需生成采样，不用定时器。
class ToneDevice : public QIODevice {
public:
    explicit ToneDevice(std::vector<harmonica::PreviewNote> notes, int sampleRate);

protected:
    qint64 readData(char* data, qint64 maxLength) override;
    qint64 writeData(const char* data, qint64 maxLength) override;

private:
    std::vector<harmonica::PreviewNote> notes_;
    int sampleRate_ = 44100;
    qint64 position_ = 0;   ///< 已经生成了多少个采样
    std::size_t cursor_ = 0;  ///< 当前音符的游标（音符本来就按时间排过序）
};

class PreviewPlayer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)

public:
    explicit PreviewPlayer(QObject* parent = nullptr);
    ~PreviewPlayer() override;

    /// 放一份 `.dhs` 文本。解析失败时返回 false 并给出原因，不放任何东西
    Q_INVOKABLE bool play(const QString& dhsText);
    Q_INVOKABLE void stop();

    [[nodiscard]] bool isPlaying() const { return sink_ != nullptr; }

signals:
    void playingChanged();
    void failed(QString reason);

private:
    QAudioSink* sink_ = nullptr;
    ToneDevice* source_ = nullptr;
};

}  // namespace harmonica::app
