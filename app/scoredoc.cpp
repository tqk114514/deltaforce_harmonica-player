#include "scoredoc.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QJsonValue>

namespace harmonica::editor {
namespace {

/// 十二平均律：半音编号 -> 简谱度数 + 是否升号
const int PC_TO_DEGREE[12][2] = {{1, 0}, {1, 1}, {2, 0}, {2, 1}, {3, 0}, {4, 0},
                                 {4, 1}, {5, 0}, {5, 1}, {6, 0}, {6, 1}, {7, 0}};
int degreeToPc(int degree) {
    static const int table[8] = {0, 0, 2, 4, 5, 7, 9, 11};  // 下标 1..7
    return degree >= 1 && degree <= 7 ? table[degree] : 0;
}

int clampInt(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

/// 音区 -> 八度偏移，超高音区按两个点算
bool samePitch(const Note& a, const Note& b) {
    return a.degree == b.degree && a.octave == b.octave && a.sharp == b.sharp;
}

/// 抄一段到 out 末尾（去掉反复记号本身，免得嵌套；连音线索引映射到副本上）
void copySegment(const std::vector<Note>& notes, std::vector<Note>& out, int from, int to) {
    const int base = static_cast<int>(out.size());
    for (int k = from; k <= to; ++k) {
        Note copy = notes[k];
        copy.rstart = false;
        copy.rend = false;
        std::vector<int> ties;
        for (const int target : copy.ties) {
            if (target >= from && target <= to) ties.push_back(base + (target - from));
        }
        copy.ties = ties;
        out.push_back(copy);
    }
}

QJsonObject compactGrace(const Grace& grace) {
    QJsonObject out;
    out[QStringLiteral("degree")] = grace.degree;
    if (grace.octave != 0) out[QStringLiteral("octave")] = grace.octave;
    if (grace.sharp) out[QStringLiteral("sharp")] = true;
    if (grace.dur != 16) out[QStringLiteral("dur")] = grace.dur;
    return out;
}

Grace normalizeGrace(const QJsonObject& object) {
    Grace grace;
    grace.degree = clampInt(object.value(QStringLiteral("degree")).toInt(1), 1, 7);
    if (const auto value = object.value(QStringLiteral("octave")); value.isDouble()) {
        grace.octave = clampInt(value.toInt(), -1, 2);
    }
    grace.sharp = object.value(QStringLiteral("sharp")).toBool(false);
    const int dur = object.value(QStringLiteral("dur")).toInt(16);
    grace.dur = (dur == 8 || dur == 16 || dur == 32) ? dur : 16;
    return grace;
}

QJsonObject compactNote(const Note& note) {
    QJsonObject out;
    // degree 是音符的身份而不是修饰：非休止符一律写出来，
    // 免得工程文件里出现 {} 这种看不出音高的东西
    if (!note.rest) out[QStringLiteral("degree")] = note.degree;
    if (note.rest) out[QStringLiteral("rest")] = true;
    if (note.octave != 0) out[QStringLiteral("octave")] = note.octave;
    if (note.dur != 4) out[QStringLiteral("dur")] = note.dur;
    if (note.dot) out[QStringLiteral("dot")] = true;
    if (note.hold != 0) out[QStringLiteral("hold")] = note.hold;
    if (note.sharp) out[QStringLiteral("sharp")] = true;
    if (!note.ties.empty()) {
        QJsonArray ties;
        for (const int target : note.ties) ties.append(target);
        out[QStringLiteral("ties")] = ties;
    }
    if (note.rstart) out[QStringLiteral("rstart")] = true;
    if (note.rend) out[QStringLiteral("rend")] = true;
    if (note.mod != 0) out[QStringLiteral("mod")] = note.mod;
    if (note.grace) out[QStringLiteral("grace")] = compactGrace(*note.grace);
    return out;
}

Note normalizeNote(const QJsonObject& object) {
    Note note;
    note.rest = object.value(QStringLiteral("rest")).toBool(false);
    note.degree = clampInt(object.value(QStringLiteral("degree")).toInt(1), 1, 7);
    note.octave = clampInt(object.value(QStringLiteral("octave")).toInt(0), -1, 2);
    const int dur = object.value(QStringLiteral("dur")).toInt(4);
    note.dur = (dur == 4 || dur == 8 || dur == 16 || dur == 32) ? dur : 4;
    note.dot = object.value(QStringLiteral("dot")).toBool(false);
    note.hold = std::max(0, object.value(QStringLiteral("hold")).toInt(0));
    note.sharp = object.value(QStringLiteral("sharp")).toBool(false);
    if (const auto ties = object.value(QStringLiteral("ties")); ties.isArray()) {
        for (const QJsonValue& value : ties.toArray()) {
            const int target = value.toInt(-1);
            if (target >= 0) note.ties.push_back(target);
        }
    }
    note.rstart = object.value(QStringLiteral("rstart")).toBool(false);
    note.rend = object.value(QStringLiteral("rend")).toBool(false);
    note.mod = object.value(QStringLiteral("mod")).toInt(0);
    if (const auto grace = object.value(QStringLiteral("grace")); grace.isObject()) {
        note.grace = normalizeGrace(grace.toObject());
    }
    return note;
}

}  // namespace

int ticksForDur(int dur) {
    const int safe = (dur == 4 || dur == 8 || dur == 16 || dur == 32) ? dur : 4;
    return static_cast<int>(std::lround(TICKS_PER_BEAT * 4.0 / safe));
}

int ticksOf(const Note& note) {
    const double base = TICKS_PER_BEAT * 4.0 / note.dur;
    return static_cast<int>(std::lround(base * (note.dot ? 1.5 : 1.0))) + std::max(0, note.hold);
}

int graceTicksOf(const Note& note) {
    if (!note.grace) return 0;
    return std::min(ticksForDur(note.grace->dur), ticksOf(note) / 2);
}

bool linkMerges(const std::vector<Note>& notes, int from, int to) {
    if (from < 0 || to < 0 || static_cast<size_t>(to) >= notes.size()) return false;
    const Note& a = notes[from];
    const Note& b = notes[to];
    return to == from + 1 && !a.rest && !b.rest && samePitch(a, b);
}

std::vector<MergedNote> mergeTies(const std::vector<Note>& notes) {
    std::vector<MergedNote> merged;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const Note& note = notes[i];
        if (!merged.empty() && i > 0) {
            // 看「前一个原始音符」有没有连到当前这个音。不能看合并后那一条自己的
            // ties：合并后的它是整串的开头，那样 7-7-7 一路连到第三个音就断了。
            const bool joins = linkMerges(notes, static_cast<int>(i) - 1, static_cast<int>(i));
            if (joins) {
                merged.back().totalTicks += ticksOf(note);
                continue;
            }
        }
        merged.push_back(MergedNote{note, ticksOf(note)});
    }
    return merged;
}

std::vector<Note> expandRepeats(const std::vector<Note>& notes) {
    std::vector<Note> out;
    int segStart = -1;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        out.push_back(notes[i]);
        if (notes[i].rstart) segStart = static_cast<int>(i);
        if (notes[i].rend && segStart >= 0) {
            copySegment(notes, out, segStart, static_cast<int>(i));
            segStart = -1;
        }
    }
    if (segStart >= 0 && segStart < static_cast<int>(notes.size())) {
        copySegment(notes, out, segStart, static_cast<int>(notes.size()) - 1);
    }
    return out;
}

std::vector<Note> playableNotes(const std::vector<Note>& notes) {
    // 第一遍：按**原始顺序**累加转调量，得到每个音自己的绝对移调量。
    // 先算量再展开反复 —— 反过来会让反复抄出来的那一遍把自己段内的转调标记再加一次。
    struct Shifted {
        Note note;
        int absShift = 0;
    };
    std::vector<Shifted> withShift;
    withShift.reserve(notes.size());
    int shift = 0;
    for (const Note& note : notes) {
        if (note.mod != 0) shift += note.mod;
        withShift.push_back(Shifted{note, shift});
    }

    // 第二遍：展开反复。副本带的是自己被复制那一格的移调量
    std::vector<Shifted> expanded;
    expanded.reserve(withShift.size());
    int segStart = -1;
    for (std::size_t i = 0; i < withShift.size(); ++i) {
        expanded.push_back(withShift[i]);
        if (withShift[i].note.rstart) segStart = static_cast<int>(i);
        if (withShift[i].note.rend && segStart >= 0) {
            for (int k = segStart; k <= static_cast<int>(i); ++k) {
                Shifted copy = withShift[k];
                copy.note.rstart = false;
                copy.note.rend = false;
                expanded.push_back(copy);
            }
            segStart = -1;
        }
    }
    if (segStart >= 0) {
        for (int k = segStart; k < static_cast<int>(withShift.size()); ++k) {
            Shifted copy = withShift[k];
            copy.note.rstart = false;
            copy.note.rend = false;
            expanded.push_back(copy);
        }
    }

    std::vector<Note> out;
    out.reserve(expanded.size());
    for (const Shifted& entry : expanded) {
        const Note& note = entry.note;
        if (entry.absShift == 0 || note.rest) {
            out.push_back(note);
            continue;
        }
        Note moved = note;
        const Grace tuned{note.degree, note.octave, note.sharp, note.dur};
        const Grace folded = midiToPitch(pitchToMidi(tuned) + entry.absShift);
        moved.degree = folded.degree;
        moved.octave = folded.octave;
        moved.sharp = folded.sharp;
        if (note.grace) {
            // 倚音的音高跟着一起移调；时值不参与移调，原样带过去
            Grace shifted = midiToPitch(pitchToMidi(*note.grace) + entry.absShift);
            shifted.dur = note.grace->dur;
            moved.grace = shifted;
        }
        out.push_back(moved);
    }
    return out;
}

int measureStartIndex(const std::vector<Note>& notes, int index, int meter) {
    const int barTicks = (meter > 0 ? meter : 4) * TICKS_PER_BEAT;
    int accumulated = 0;
    int start = 0;
    for (int k = 0; k <= index; ++k) {
        if (k == index) return start;
        accumulated += ticksOf(notes[k]);
        if (accumulated >= barTicks) {
            accumulated -= barTicks;
            start = k + 1;
        }
    }
    return start;
}

int pitchToMidi(const Grace& pitch) {
    const int degree = pitch.octave >= 2 ? 1 : clampInt(pitch.degree, 1, 7);
    return 60 + pitch.octave * 12 + degreeToPc(degree) + (pitch.sharp ? 1 : 0);
}

double freqOf(const Grace& pitch) {
    return 440.0 * std::pow(2.0, (pitchToMidi(pitch) - 69) / 12.0);
}

Grace midiToPitch(int midi) {
    while (midi < 48) midi += 12;   // 低于 C3 折上来
    while (midi > 84) midi -= 12;   // 高于 C6 折下去
    const int pc = ((midi % 12) + 12) % 12;
    const int octave = midi / 12 - 1;
    Grace pitch;
    pitch.degree = PC_TO_DEGREE[pc][0];
    pitch.sharp = PC_TO_DEGREE[pc][1] != 0;
    if (octave <= 3) {
        pitch.octave = -1;
    } else if (octave == 4) {
        pitch.octave = 0;
    } else if (octave == 5) {
        pitch.octave = 1;
    } else {
        pitch.octave = 2;  // 超高音区只有 do
        pitch.degree = 1;
        pitch.sharp = false;
    }
    return pitch;
}

std::expected<int, QString> modFromRelation(const QString& text, int* alternative) {
    const QString input = text.trimmed();
    if (input.isEmpty()) return std::unexpected(QStringLiteral("转调内容是空的"));

    // 也可以直接填半音数
    bool isNumber = false;
    const int direct = input.toInt(&isNumber);
    if (isNumber) {
        int value = direct;
        while (value > 6) value -= 12;
        while (value < -6) value += 12;
        if (alternative != nullptr && value != direct) *alternative = direct;
        return value;
    }

    static const QRegularExpression relation{
        QStringLiteral("^前?\\s*([1-7])(#?)\\s*=\\s*后?\\s*([1-7])(#?)$")};
    const auto match = relation.match(input);
    if (!match.hasMatch()) {
        return std::unexpected(
            QStringLiteral("看不懂「%1」：填像「前3=后5」的关系，或者直接填半音数").arg(input));
    }
    const int from = degreeToPc(match.captured(1).toInt()) + (match.captured(2) == QLatin1String("#") ? 1 : 0);
    const int to = degreeToPc(match.captured(3).toInt()) + (match.captured(4) == QLatin1String("#") ? 1 : 0);
    int value = to - from;
    if (alternative != nullptr) *alternative = value;
    while (value > 6) value -= 12;
    while (value < -6) value += 12;
    return value;
}

QString toJson(const Score& score) {
    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("harmonica-score-v1");
    root[QStringLiteral("title")] = score.meta.title;
    root[QStringLiteral("bpm")] = score.meta.bpm;
    root[QStringLiteral("meter")] = score.meta.meter;
    if (!score.meta.barlines) root[QStringLiteral("barlines")] = false;

    QJsonArray notes;
    for (const Note& note : score.notes) notes.append(compactNote(note));
    root[QStringLiteral("notes")] = notes;
    return QString::fromUtf8(QJsonDocument{root}.toJson(QJsonDocument::Indented));
}

std::expected<Score, QString> fromJson(const QString& text) {
    const auto parsed = QJsonDocument::fromJson(text.toUtf8());
    if (parsed.isNull() || !parsed.isObject()) {
        return std::unexpected(QStringLiteral("这不是一份工程文件（不是 JSON 对象）"));
    }
    const QJsonObject root = parsed.object();
    const QString format = root.value(QStringLiteral("format")).toString();
    if (format != QStringLiteral("harmonica-score-v1")) {
        return std::unexpected(
            QStringLiteral("工程文件格式不认识：%1").arg(format.isEmpty() ? QStringLiteral("(没有 format)") : format));
    }

    Score score;
    score.meta.title = root.value(QStringLiteral("title")).toString();
    score.meta.bpm = clampInt(root.value(QStringLiteral("bpm")).toInt(120), 1, 400);
    score.meta.meter = root.value(QStringLiteral("meter")).toInt(4);
    score.meta.barlines = root.value(QStringLiteral("barlines")).toBool(true);
    if (const auto notes = root.value(QStringLiteral("notes")); notes.isArray()) {
        for (const QJsonValue& value : notes.toArray()) {
            if (value.isObject()) score.notes.push_back(normalizeNote(value.toObject()));
        }
    }
    return score;
}

QString toDhs(const Score& score) {
    QStringList lines;
    lines << QStringLiteral("# 由简谱编辑器生成")
          << QStringLiteral("title=%1").arg(score.meta.title.isEmpty() ? QStringLiteral("我的曲子")
                                                                      : score.meta.title)
          << QStringLiteral("bpm=%1").arg(score.meta.bpm)
          << QStringLiteral("ticks_per_beat=%1").arg(TICKS_PER_BEAT)
          << QStringLiteral("")
          << QStringLiteral("[score]");

    QStringList tokens;
    for (const MergedNote& merged : mergeTies(playableNotes(score.notes))) {
        const Note& note = merged.note;
        if (note.rest) {
            tokens << QStringLiteral("R/%1").arg(merged.totalTicks);
            continue;
        }
        const int degree = note.octave >= 2 ? 1 : note.degree;
        const QString region = note.octave <= -1 ? QStringLiteral("L")
                           : note.octave == 0    ? QStringLiteral("M")
                           : note.octave == 1    ? QStringLiteral("H")
                                                 : QStringLiteral("T");
        const QString main = QStringLiteral("%1%2%3")
                                 .arg(region)
                                 .arg(degree)
                                 .arg(note.sharp ? QStringLiteral("#") : QString());
        if (!note.grace) {
            tokens << QStringLiteral("%1/%2").arg(main).arg(merged.totalTicks);
            continue;
        }
        // 倚音写成它自己的一个短音，并从主音借走同样多的时间：演奏器看到的就是
        // 两个普通音符，那边不需要任何改动，整首曲子的总时长也不变
        const int borrowed = std::min(ticksForDur(note.grace->dur), merged.totalTicks / 2);
        const QString graceRegion = note.grace->octave <= -1 ? QStringLiteral("L")
                                      : note.grace->octave == 0 ? QStringLiteral("M")
                                      : note.grace->octave == 1 ? QStringLiteral("H")
                                                                : QStringLiteral("T");
        const int graceDegree = note.grace->octave >= 2 ? 1 : note.grace->degree;
        tokens << QStringLiteral("%1%2%3/%4")
                      .arg(graceRegion)
                      .arg(graceDegree)
                      .arg(note.grace->sharp ? QStringLiteral("#") : QString())
                      .arg(borrowed)
               << QStringLiteral("%1/%2").arg(main).arg(merged.totalTicks - borrowed);
    }

    for (int i = 0; i < tokens.size(); i += 12) {
        lines << tokens.mid(i, 12).join(QLatin1Char(' '));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

std::expected<Score, QString> fromDhs(const QString& text) {
    Score score;
    score.meta.title = QStringLiteral("导入的演奏谱");
    bool inScore = false;
    double ticksPerBeat = TICKS_PER_BEAT;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.compare(QStringLiteral("[score]"), Qt::CaseInsensitive) == 0) {
            inScore = true;
            continue;
        }
        if (!inScore) {
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals < 0) continue;
            const QString key = line.sliced(0, equals).trimmed().toLower();
            const QString value = line.sliced(equals + 1).trimmed();
            if (key == QLatin1String("title")) score.meta.title = value;
            else if (key == QLatin1String("bpm")) score.meta.bpm = clampInt(value.toInt(), 1, 400);
            else if (key == QLatin1String("ticks_per_beat")) ticksPerBeat = std::max(1.0, value.toDouble());
            continue;
        }

        for (const QString& token : line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                Qt::SkipEmptyParts)) {
            static const QRegularExpression pattern{
                QStringLiteral("^([LMHTR])([1-7]?)(#?)/(\\d+)$")};
            const auto match = pattern.match(token);
            if (!match.hasMatch()) {
                return std::unexpected(QStringLiteral("看不懂这个音：%1").arg(token));
            }
            const QString region = match.captured(1);
            const int rawTicks = match.captured(4).toInt();
            if (rawTicks <= 0) return std::unexpected(QStringLiteral("时值必须大于 0：%1").arg(token));

            Note note;
            if (region == QLatin1String("R")) {
                note.rest = true;
            } else {
                note.octave = region == QLatin1String("L") ? -1
                              : region == QLatin1String("M") ? 0
                              : region == QLatin1String("H") ? 1 : 2;
                note.degree = note.octave >= 2 ? 1 : match.captured(2).toInt();
                note.sharp = match.captured(3) == QLatin1String("#");
            }
            // tick 数换算回编辑器那一套：能整除的取最短写法，剩下的进增时线。
            // 连音线 / 反复 / 转调 / 倚音在导出时已经展开成实际音符，恢复不了
            const double scaled = rawTicks * (TICKS_PER_BEAT / ticksPerBeat);
            const int ticks = static_cast<int>(std::lround(scaled));
            for (const int dur : {4, 8, 16, 32}) {
                const int base = ticksForDur(dur);
                if (ticks >= base && (ticks - base) % TICKS_PER_BEAT == 0) {
                    note.dur = dur;
                    note.hold = ticks - base;
                    break;
                }
                if (ticks >= base * 3 / 2 && (ticks - base * 3 / 2) % TICKS_PER_BEAT == 0) {
                    note.dur = dur;
                    note.dot = true;
                    note.hold = ticks - base * 3 / 2;
                    break;
                }
            }
            if (note.dur == 4 && note.hold == 0 && ticks != TICKS_PER_BEAT) {
                note.hold = std::max(0, ticks - TICKS_PER_BEAT);
            }
            score.notes.push_back(note);
        }
    }
    if (score.notes.empty()) return std::unexpected(QStringLiteral("没读到任何音符"));
    return score;
}

std::vector<PlacedNote> layout(const Score& score) {
    const int barTicks = (score.meta.meter > 0 ? score.meta.meter : 4) * TICKS_PER_BEAT;
    std::vector<PlacedNote> out;
    int measure = 0;
    int tickInBar = 0;
    const Note* previous = nullptr;
    int previousBeat = -1;
    int previousLevels = 0;

    for (std::size_t i = 0; i < score.notes.size(); ++i) {
        const Note& note = score.notes[i];
        PlacedNote placed;
        placed.index = static_cast<int>(i);
        placed.measure = score.meta.barlines ? measure : 0;
        placed.measureStart = tickInBar == 0;
        placed.beamLevels = note.dur == 32 ? 3 : note.dur == 16 ? 2 : note.dur == 8 ? 1 : 0;
        placed.isRest = note.rest;
        placed.sharp = note.sharp;
        placed.octave = note.octave;
        placed.degree = note.degree;
        placed.holdBars = note.hold / TICKS_PER_BEAT;
        placed.dot = note.dot;
        placed.rstart = note.rstart;
        placed.rend = note.rend;
        placed.tieTargets = static_cast<int>(note.ties.size());
        placed.grace = note.grace;
        if (note.mod != 0) {
            placed.modLabel = QStringLiteral("%1%2").arg(note.mod > 0 ? QStringLiteral("+")
                                                                      : QString())
                                               .arg(note.mod);
        }

        const int beat = tickInBar / TICKS_PER_BEAT;
        placed.beamSharedWithPrev = previous != nullptr && !placed.measureStart
                                    && beat == previousBeat
                                    && placed.beamLevels == previousLevels && placed.beamLevels > 0;

        tickInBar += ticksOf(note);
        while (tickInBar >= barTicks) {
            tickInBar -= barTicks;
            measure += 1;
        }
        previous = &note;
        previousBeat = beat;  // 前一音**自己**在第几拍，不是推进之后的位置
        previousLevels = placed.beamLevels;
        out.push_back(placed);
    }
    return out;
}

QVariantList layoutAsVariantList(const std::vector<PlacedNote>& placed) {
    QVariantList out;
    for (const PlacedNote& item : placed) {
        QVariantMap row{
            {QStringLiteral("index"), item.index},
            {QStringLiteral("measure"), item.measure},
            {QStringLiteral("measureStart"), item.measureStart},
            {QStringLiteral("beamLevels"), item.beamLevels},
            {QStringLiteral("beamSharedWithPrev"), item.beamSharedWithPrev},
            {QStringLiteral("tieTargets"), item.tieTargets},
            {QStringLiteral("rest"), item.isRest},
            {QStringLiteral("sharp"), item.sharp},
            {QStringLiteral("octave"), item.octave},
            {QStringLiteral("degree"), item.degree},
            {QStringLiteral("holdBars"), item.holdBars},
            {QStringLiteral("dot"), item.dot},
            {QStringLiteral("rstart"), item.rstart},
            {QStringLiteral("rend"), item.rend},
            {QStringLiteral("modLabel"), item.modLabel},
        };
        out.push_back(row);
    }
    return out;
}

}  // namespace harmonica::editor
