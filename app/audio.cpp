#include "audio.h"

#include <algorithm>
#include <cmath>

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QTimer>

#include "preview.h"
#include "score.h"

namespace harmonica::app {
namespace {

constexpr int SAMPLE_RATE = 44100;
/// 中音区 do 的频率（C4）。其它音按 core 给的半音数折算
constexpr double MIDDLE_C4 = 261.6255653005986;

double frequencyOf(int semitone) {
    return MIDDLE_C4 * std::pow(2.0, semitone / 12.0);
}

}  // namespace

ToneDevice::ToneDevice(std::vector<harmonica::PreviewNote> notes, int sampleRate)
    : notes_{std::move(notes)}, sampleRate_{sampleRate} {
    open(QIODevice::ReadOnly);
}

qint64 ToneDevice::readData(char* data, qint64 maxLength) {
    if (notes_.empty()) return 0;

    constexpr qint64 bytesPerFrame = 2;  // 16 位单声道
    const qint64 frames = maxLength / bytesPerFrame;
    auto* out = reinterpret_cast<qint16*>(data);

    for (qint64 i = 0; i < frames; ++i) {
        const qint64 sample = position_ + i;
        const double atMs = static_cast<double>(sample) * 1000.0 / sampleRate_;

        // 音符本来就按时间排过序，用游标推进，别每个采样从头扫一遍
        while (cursor_ + 1 < notes_.size() && notes_[cursor_ + 1].atMs <= atMs) ++cursor_;
        const harmonica::PreviewNote& note = notes_[cursor_];

        double value = 0.0;
        if (atMs >= note.atMs && atMs < note.atMs + note.durMs) {
            const double localMs = atMs - note.atMs;
            // 两头各留几毫秒淡入淡出，不然每个音开头都是一声「咔」
            const double envelope =
                std::clamp(std::min(localMs / 8.0, (note.durMs - localMs) / 12.0), 0.0, 1.0);
            value = std::sin(2.0 * M_PI * frequencyOf(note.semitone) * sample / sampleRate_) * 0.3 * envelope;
        } else if (atMs >= note.atMs + note.durMs && cursor_ + 1 == notes_.size()) {
            return 0;  // 最后一个音已经放完
        }
        out[i] = static_cast<qint16>(value * 32767.0);
    }

    position_ += frames;
    return frames * bytesPerFrame;
}

qint64 ToneDevice::writeData(const char*, qint64) {
    return 0;
}

PreviewPlayer::PreviewPlayer(QObject* parent) : QObject{parent} {}

PreviewPlayer::~PreviewPlayer() {
    stop();
}

bool PreviewPlayer::play(const QString& dhsText) {
    stop();

    const auto song = harmonica::parseScore(dhsText);
    if (!song) {
        emit failed(song.error());
        return false;
    }
    const auto notes = harmonica::previewNotes(*song);   // 时值和音高都由 core 算
    if (notes.empty()) {
        emit failed(QStringLiteral("这份谱子里没有可听的音"));
        return false;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    QAudioFormat format = device.preferredFormat();
    format.setSampleRate(SAMPLE_RATE);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        emit failed(QStringLiteral("声卡不支持这套采样格式"));
        return false;
    }

    source_ = new ToneDevice{notes, format.sampleRate()};
    sink_ = new QAudioSink{device, format, this};
    sink_->start(source_);

    // 放完之后自己收摊：ToneDevice 报 EOF 之后 QAudioSink 会静默停在
    // 「没有数据」的状态，这里按谱面总时长定时收尾，界面才知道已经完了
    const double totalMs = song->durationMs() + 400.0;
    QTimer::singleShot(static_cast<int>(totalMs), this, [this] {
        if (sink_ != nullptr) stop();
    });

    emit playingChanged();
    return true;
}

void PreviewPlayer::stop() {
    if (sink_ == nullptr) return;
    sink_->stop();
    delete sink_;
    sink_ = nullptr;
    if (source_ != nullptr) {
        source_->deleteLater();
        source_ = nullptr;
    }
    emit playingChanged();
}

}  // namespace harmonica::app
