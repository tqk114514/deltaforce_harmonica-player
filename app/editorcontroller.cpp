#include "editorcontroller.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "preview.h"
#include "score.h"

namespace harmonica::app {
namespace {

constexpr const char* AUTOSAVE_NAME = "autosave.score.json";

/// 只接受纯粹的文件名，并且必须以指定后缀结尾 —— 和保存下载那边同一套规矩
bool isSafeName(const QString& name, const QString& suffix) {
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
           && !name.contains(QLatin1Char(':')) && !name.contains(QStringLiteral(".."))
           && name.endsWith(suffix, Qt::CaseInsensitive);
}

bool writeTextFile(const QString& path, const QString& text) {
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(text.toUtf8()) >= 0;
}

QString readTextFile(const QString& path) {
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}

/// 删掉一个音之后，指着它的连音线要跟着收拾
void repairTies(std::vector<editor::Note>& notes, int removed) {
    for (editor::Note& note : notes) {
        std::vector<int> kept;
        for (const int target : note.ties) {
            if (target == removed) continue;
            kept.push_back(target > removed ? target - 1 : target);
        }
        note.ties = kept;
    }
}

}  // namespace

EditorController::EditorController(Layout layout, QObject* parent)
    : QObject{parent}, layout_{std::move(layout)} {
    score_.meta.title = QStringLiteral("我的曲子");
    refreshProjects();
}

QVariantList EditorController::placed() const {
    QVariantList out;
    const auto laid = editor::layout(score_);
    for (const editor::PlacedNote& item : laid) {
        const editor::Note& note = score_.notes[item.index];
        out.push_back(QVariantMap{
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
            {QStringLiteral("dur"), note.dur},
            {QStringLiteral("links"), QVariantList{note.ties.begin(), note.ties.end()}},
            {QStringLiteral("grace"), note.grace ? QVariantMap{
                    {QStringLiteral("degree"), note.grace->degree},
                    {QStringLiteral("octave"), note.grace->octave},
                    {QStringLiteral("sharp"), note.grace->sharp},
                    {QStringLiteral("dur"), note.grace->dur}}
                                                : QVariantMap{}},
        });
    }
    return out;
}

void EditorController::setStatus(QString text) {
    status_ = std::move(text);
    emit statusChanged();
}

void EditorController::setTitle(QString title) {
    score_.meta.title = std::move(title);
    emit documentChanged();
}

void EditorController::setBpm(int bpm) {
    score_.meta.bpm = std::clamp(bpm, 20, 400);
    emit documentChanged();
}

void EditorController::setMeter(int meter) {
    score_.meta.meter = std::clamp(meter, 1, 12);
    emit documentChanged();
}

void EditorController::setBarlines(bool on) {
    score_.meta.barlines = on;
    emit documentChanged();
}

void EditorController::setCurDur(int dur) {
    if (dur != 4 && dur != 8 && dur != 16 && dur != 32) return;
    curDur_ = dur;
    emit cursChanged();
}

void EditorController::setCurDot(bool on) {
    curDot_ = on;
    emit cursChanged();
}

void EditorController::setCurSharp(bool on) {
    curSharp_ = on;
    emit cursChanged();
}

void EditorController::setCurOctave(int octave) {
    curOctave_ = std::clamp(octave, -1, 2);
    emit cursChanged();
}

void EditorController::noteAdded() {
    emit documentChanged();
}

void EditorController::append(int degree) {
    editor::Note note;
    if (degree == 0) {
        note.rest = true;
    } else {
        note.degree = std::clamp(degree, 1, 7);
        note.sharp = curSharp_;
        note.octave = curOctave_;
    }
    note.dur = curDur_;
    note.dot = curDot_;
    // 超高音区只有 do：录别的度数时按 do 收，别让非法音位进谱子
    if (note.octave >= 2 && !note.rest) note.degree = 1;

    score_.notes.push_back(note);
    selected_ = static_cast<int>(score_.notes.size()) - 1;
    noteAdded();
}

void EditorController::select(int index) {
    if (index >= static_cast<int>(score_.notes.size())) index = -1;
    // 点下一个音就等于把连音线的起点交出去了吗？不 —— 只有显式 pick 才算，
    // 这里选中别的音时保持起点不变，方便连完之后继续改别的音
    selected_ = index;
    if (selected_ >= 0) {
        const editor::Note& note = score_.notes[selected_];
        curDur_ = note.dur;
        curDot_ = note.dot;
        curSharp_ = note.sharp;
        curOctave_ = note.octave;
        emit cursChanged();
    }
    emit documentChanged();
}

void EditorController::applyToSelection() {
    const auto index = selectedIndex();
    if (!index) return;
    editor::Note& note = score_.notes[*index];
    if (note.rest) return;
    note.dur = curDur_;
    note.dot = curDot_;
    note.sharp = curSharp_;
    note.octave = curOctave_;
    if (note.octave >= 2) {
        note.degree = 1;
        note.sharp = false;
    }
    emit documentChanged();
}

std::optional<int> EditorController::selectedIndex() const {
    if (selected_ < 0 || selected_ >= static_cast<int>(score_.notes.size())) return std::nullopt;
    return selected_;
}

void EditorController::removeSelected() {
    const auto index = selectedIndex();
    if (!index) return;
    score_.notes.erase(score_.notes.begin() + *index);
    repairTies(score_.notes, *index);
    if (tieFrom_ == *index) tieFrom_ = -1;
    else if (tieFrom_ > *index) tieFrom_ -= 1;
    selected_ = std::min(*index - 1, static_cast<int>(score_.notes.size()) - 1);
    emit documentChanged();
}

void EditorController::appendHold() {
    const auto index = selectedIndex();
    if (!index) {
        setStatus(QStringLiteral("先点一个音，再延长一拍"));
        return;
    }
    score_.notes[*index].hold += editor::TICKS_PER_BEAT;
    emit documentChanged();
}

void EditorController::removeHold() {
    const auto index = selectedIndex();
    if (!index) return;
    score_.notes[*index].hold = std::max(0, score_.notes[*index].hold - editor::TICKS_PER_BEAT);
    emit documentChanged();
}

void EditorController::moveSelected(int delta) {
    const auto index = selectedIndex();
    if (!index || delta == 0) return;
    const int to = *index + delta;
    if (to < 0 || to >= static_cast<int>(score_.notes.size())) return;

    std::swap(score_.notes[*index], score_.notes[to]);
    // 连音线跟着音走：指着这两个位置的要换成新位置
    for (editor::Note& note : score_.notes) {
        for (int& target : note.ties) {
            if (target == *index) target = to;
            else if (target == to) target = *index;
        }
    }
    if (tieFrom_ == *index) tieFrom_ = to;
    else if (tieFrom_ == to) tieFrom_ = *index;
    selected_ = to;
    emit documentChanged();
}

void EditorController::pickTieTarget(int index) {
    if (index < 0 || index >= static_cast<int>(score_.notes.size())) return;
    if (tieFrom_ < 0) {
        tieFrom_ = index;
        selected_ = index;
        emit documentChanged();
        return;
    }
    if (index == tieFrom_) {
        setStatus(QStringLiteral("不能连到自己"));
        return;
    }
    auto& from = score_.notes[tieFrom_];
    if (std::find(from.ties.begin(), from.ties.end(), index) == from.ties.end()) {
        from.ties.push_back(index);
        std::sort(from.ties.begin(), from.ties.end());
    }
    const bool merges = editor::linkMerges(score_.notes, tieFrom_, index);
    setStatus(merges ? QStringLiteral("已连成延音线（演奏时合并成一个长音）")
                     : QStringLiteral("已连成圆滑线（只画在谱面上，不改变演奏）"));
    tieFrom_ = -1;
    emit documentChanged();
}

void EditorController::cancelTie() {
    tieFrom_ = -1;
    emit documentChanged();
}

void EditorController::toggleRepeatStart() {
    const auto index = selectedIndex();
    if (!index) return;
    score_.notes[*index].rend = false;
    score_.notes[*index].rstart = !score_.notes[*index].rstart;
    emit documentChanged();
}

void EditorController::toggleRepeatEnd() {
    const auto index = selectedIndex();
    if (!index) return;
    score_.notes[*index].rstart = false;
    score_.notes[*index].rend = !score_.notes[*index].rend;
    emit documentChanged();
}

bool EditorController::setMod(const QString& relation) {
    int alternative = 0;
    const auto value = editor::modFromRelation(relation, &alternative);
    if (!value) {
        setStatus(value.error());
        return false;
    }
    const auto index = selectedIndex();
    if (!index) {
        setStatus(QStringLiteral("先点一个音，再定转调"));
        return false;
    }
    // 转调标记画在小节线上，所以从该小节的第一个音生效
    const int start = editor::measureStartIndex(score_.notes, *index, score_.meta.meter);
    score_.notes[start].mod = *value;
    QString why = QStringLiteral("转调 %1 个半音，从小节 %2 的第一个音生效")
                      .arg(*value)
                      .arg(score_.meta.barlines ? start + 1 : 1);
    if (*value != alternative) {
        // 移调量超过 6 个半音时，反方向差一个八度是等价的、更省事
        why += QStringLiteral("（也可以填 %1，差一个八度）").arg(alternative);
    }
    setStatus(why);
    emit documentChanged();
    return true;
}

void EditorController::clearMod() {
    const auto index = selectedIndex();
    if (!index) return;
    const int start = editor::measureStartIndex(score_.notes, *index, score_.meta.meter);
    score_.notes[start].mod = 0;
    emit documentChanged();
}

QString EditorController::graceHint(int degree, int octave, bool sharp, int dur) const {
    const auto index = selectedIndex();
    editor::Note probe = index ? score_.notes[*index] : editor::Note{};
    probe.grace = editor::Grace{degree, std::min(octave, 2), sharp, dur};

    const int ticks = editor::graceTicksOf(probe);
    const double ms = ticks * 60000.0 / score_.meta.bpm / editor::TICKS_PER_BEAT;
    const editor::Grace shown{degree, std::min(octave, 2), sharp, dur};
    const int region = shown.octave <= -1 ? 0 : shown.octave == 0 ? 1 : shown.octave == 1 ? 2 : 3;
    static const char* REGION[] = {"低", "", "高", "超高"};
    QString text = QStringLiteral("%1%2%3 · %4 tick · %5 ms")
                       .arg(QString::fromUtf8(REGION[region]))
                       .arg(shown.octave >= 2 ? 1 : shown.degree)
                       .arg(shown.sharp ? QStringLiteral("#") : QString())
                       .arg(ticks)
                       .arg(QString::number(ms, 'f', 1));
    if (index) {
        text += QStringLiteral("，从主音（%1 tick）里借").arg(editor::ticksOf(score_.notes[*index]));
        // 想借的和实际借到的不一样，就说明主音太短、被压到一半上限了
        if (editor::ticksForDur(dur) > ticks) {
            text += QStringLiteral(" —— 主音太短，已压到时值的一半（%1 tick 上限）")
                        .arg(editor::ticksOf(probe) / 2);
        }
    }
    return text;
}

void EditorController::setGrace(int degree, int octave, bool sharp, int dur) {
    const auto index = selectedIndex();
    if (!index) {
        setStatus(QStringLiteral("先点一个主音，再加倚音"));
        return;
    }
    score_.notes[*index].grace = editor::Grace{degree, std::clamp(octave, -1, 2), sharp,
                                              (dur == 8 || dur == 32) ? dur : 16};
    emit documentChanged();
}

void EditorController::clearGrace() {
    const auto index = selectedIndex();
    if (!index) return;
    score_.notes[*index].grace = std::nullopt;
    emit documentChanged();
}

void EditorController::clearAll() {
    score_.notes.clear();
    selected_ = -1;
    tieFrom_ = -1;
    emit documentChanged();
}

void EditorController::refreshProjects() {
    projects_.clear();
    const QDir dir{layout_.notationDir};
    if (!dir.exists()) {
        emit statusChanged();
        return;
    }
    const auto entries = dir.entryInfoList({QStringLiteral("*.score.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : entries) projects_ << info.fileName();
    emit statusChanged();
}

bool EditorController::saveProject(const QString& name) {
    if (!isSafeName(name, QStringLiteral(".score.json"))) {
        setStatus(QStringLiteral("文件名要是纯名字并以 .score.json 结尾：%1").arg(name));
        return false;
    }
    if (!ensureDirs(layout_).isEmpty()) {
        setStatus(QStringLiteral("曲谱目录不可用"));
        return false;
    }
    const QString path = QDir{layout_.notationDir}.filePath(name);
    if (!writeTextFile(path, editor::toJson(score_))) {
        setStatus(QStringLiteral("写入 %1 失败").arg(path));
        return false;
    }
    openName_ = name;
    setStatus(QStringLiteral("已保存 %1").arg(name));
    refreshProjects();
    return true;
}

bool EditorController::openProject(const QString& name) {
    if (!isSafeName(name, QStringLiteral(".score.json"))) return false;
    const auto parsed = editor::fromJson(readTextFile(QDir{layout_.notationDir}.filePath(name)));
    if (!parsed) {
        setStatus(parsed.error());
        return false;
    }
    score_ = *parsed;
    openName_ = name;
    selected_ = -1;
    tieFrom_ = -1;
    emit documentChanged();
    setStatus(QStringLiteral("已打开 %1").arg(name));
    return true;
}

bool EditorController::openDhs(const QString& name) {
    if (!isSafeName(name, QStringLiteral(".dhs"))) return false;
    const QString path = QDir{layout_.dhsDir}.filePath(name);
    auto parsed = editor::fromDhs(readTextFile(path));
    if (!parsed) {
        setStatus(parsed.error());
        return false;
    }
    parsed->meta.meter = score_.meta.meter;   // 拍号是排版参数，导入的谱子里没有，沿用当前的
    score_ = *parsed;
    openName_.clear();
    selected_ = -1;
    tieFrom_ = -1;
    emit documentChanged();
    setStatus(QStringLiteral("已导入 %1：连音线、反复、转调、倚音在导出时已经展开成实际音符，"
                             "这四类标记恢复不了；要继续编辑请存成工程文件")
                  .arg(name));
    return true;
}

bool EditorController::exportDhs(const QString& name) {
    if (!isSafeName(name, QStringLiteral(".dhs"))) {
        setStatus(QStringLiteral("导出文件名要是纯名字并以 .dhs 结尾：%1").arg(name));
        return false;
    }
    if (!ensureDirs(layout_).isEmpty()) {
        setStatus(QStringLiteral("曲谱目录不可用"));
        return false;
    }
    const QString path = QDir{layout_.dhsDir}.filePath(name);
    if (!writeTextFile(path, dhsText())) {
        setStatus(QStringLiteral("写入 %1 失败").arg(path));
        return false;
    }
    setStatus(QStringLiteral("已导出 %1，曲库刷新就能看到").arg(name));
    return true;
}

QString EditorController::dhsText() const {
    return editor::toDhs(score_);
}

bool EditorController::hasAutosave() const {
    return QFileInfo::exists(QDir{layout_.root}.filePath(QLatin1String{AUTOSAVE_NAME}));
}

bool EditorController::autosave() {
    return writeTextFile(QDir{layout_.root}.filePath(QLatin1String{AUTOSAVE_NAME}),
                         editor::toJson(score_));
}

bool EditorController::restoreAutosave() {
    const QString path = QDir{layout_.root}.filePath(QLatin1String{AUTOSAVE_NAME});
    const auto parsed = editor::fromJson(readTextFile(path));
    if (!parsed) return false;
    score_ = *parsed;
    selected_ = -1;
    tieFrom_ = -1;
    emit documentChanged();
    return true;
}

}  // namespace harmonica::app
