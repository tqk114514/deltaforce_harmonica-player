#include "preview.h"

namespace harmonica {
namespace {

/// 音区 → 八度偏移。低音区比中音区低一个八度，高音区高一个，超高音区高两个。
int octaveOf(Register register_) {
    switch (register_) {
        case Register::Low: return -1;
        case Register::Mid: return 0;
        case Register::High: return 1;
        case Register::Top: return 2;
    }
    return 0;
}

/// 简谱度数 → 大调音阶的半音数
constexpr int DEGREE_SEMITONES[7] = {0, 2, 4, 5, 7, 9, 11};

}  // namespace

int semitoneOf(const Pitch& pitch) {
    const int degree = std::clamp<int>(pitch.degree, 1, 7) - 1;
    const int sharp = pitch.sharp ? 1 : 0;
    return DEGREE_SEMITONES[degree] + sharp + octaveOf(pitch.register_) * 12;
}

std::vector<PreviewNote> previewNotes(const Song& song) {
    const double msPerTick = song.msPerTick();
    std::vector<PreviewNote> out;
    double cursor = 0.0;

    for (const Event& event : song.events) {
        const double dur = static_cast<double>(ticksOf(event)) * msPerTick;
        if (const auto* note = std::get_if<NoteEvent>(&event)) {
            out.push_back(PreviewNote{cursor, dur, semitoneOf(note->pitch)});
        }
        cursor += dur;
    }
    return out;
}

}  // namespace harmonica
