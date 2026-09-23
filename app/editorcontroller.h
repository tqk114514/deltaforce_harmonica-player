#pragma once

//! 简谱编辑器的界面后端：持有当前谱子，把每一次编辑转成对音符数组的操作。
//!
//! 所有算法都在 `scoredoc`（时值、延音线合并、反复展开、转调、倚音、排版、
//! 两种文件格式）—— 这里只改数据、不重算规则。界面上看到的小节线、减时线、
//! 连音线全都来自 `layout()` 算出来的那一份。

#include <optional>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "paths.h"
#include "scoredoc.h"

namespace harmonica::app {

class EditorController : public QObject {
    Q_OBJECT

    /// 排版后的音符列表，界面照着画
    Q_PROPERTY(QVariantList placed READ placed NOTIFY documentChanged)
    Q_PROPERTY(int count READ count NOTIFY documentChanged)
    Q_PROPERTY(int selected READ selected NOTIFY documentChanged)
    /// 连音线的起点（-1 = 没在连）。起点选定后界面要提示「再点一个终点」
    Q_PROPERTY(int tieFrom READ tieFrom NOTIFY documentChanged)

    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY documentChanged)
    Q_PROPERTY(int bpm READ bpm WRITE setBpm NOTIFY documentChanged)
    Q_PROPERTY(int meter READ meter WRITE setMeter NOTIFY documentChanged)
    Q_PROPERTY(bool barlines READ barlines WRITE setBarlines NOTIFY documentChanged)

    /// 下一个音用什么（录入时的当前属性）
    Q_PROPERTY(int curDur READ curDur WRITE setCurDur NOTIFY cursChanged)
    Q_PROPERTY(bool curDot READ curDot WRITE setCurDot NOTIFY cursChanged)
    Q_PROPERTY(bool curSharp READ curSharp WRITE setCurSharp NOTIFY cursChanged)
    Q_PROPERTY(int curOctave READ curOctave WRITE setCurOctave NOTIFY cursChanged)

    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QStringList projects READ projects NOTIFY statusChanged)
    /// 正在改的那份工程的名字；空 = 还没存过
    Q_PROPERTY(QString openName READ openName NOTIFY statusChanged)

public:
    explicit EditorController(Layout layout, QObject* parent = nullptr);

    [[nodiscard]] QVariantList placed() const;
    [[nodiscard]] int count() const { return static_cast<int>(score_.notes.size()); }
    [[nodiscard]] int selected() const { return selected_; }
    [[nodiscard]] int tieFrom() const { return tieFrom_; }
    [[nodiscard]] QString title() const { return score_.meta.title; }
    [[nodiscard]] int bpm() const { return score_.meta.bpm; }
    [[nodiscard]] int meter() const { return score_.meta.meter; }
    [[nodiscard]] bool barlines() const { return score_.meta.barlines; }
    [[nodiscard]] int curDur() const { return curDur_; }
    [[nodiscard]] bool curDot() const { return curDot_; }
    [[nodiscard]] bool curSharp() const { return curSharp_; }
    [[nodiscard]] int curOctave() const { return curOctave_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QStringList projects() const { return projects_; }
    [[nodiscard]] QString openName() const { return openName_; }

    void setTitle(QString title);
    void setBpm(int bpm);
    void setMeter(int meter);
    void setBarlines(bool on);
    void setCurDur(int dur);
    void setCurDot(bool on);
    void setCurSharp(bool on);
    void setCurOctave(int octave);

    // ------------------------------------------------------------- 录入与修改
    /// 追加一个音（degree=0 是休止符），用当前的录入属性
    Q_INVOKABLE void append(int degree);
    Q_INVOKABLE void select(int index);
    /// 把当前录入属性刷到选中的音上（点按钮改时值/八度/升号就是这个）
    Q_INVOKABLE void applyToSelection();
    Q_INVOKABLE void removeSelected();
    /// 增时线：给选中的音（通常是刚录入的那个）右边再加一拍
    Q_INVOKABLE void appendHold();
    Q_INVOKABLE void removeHold();
    Q_INVOKABLE void moveSelected(int delta);
    /// 连音线：第一次调用选起点，第二次连到终点。同一个音可以连多个目标
    Q_INVOKABLE void pickTieTarget(int index);
    Q_INVOKABLE void cancelTie();
    Q_INVOKABLE void toggleRepeatStart();
    Q_INVOKABLE void toggleRepeatEnd();
    /// 转调：填「前3=后5」或直接填半音数。结果会对齐到本小节第一个音
    Q_INVOKABLE bool setMod(const QString& relation);
    Q_INVOKABLE void clearMod();
    /// 倚音面板用：先问一声「这么填会占几个 tick / 多少毫秒」
    Q_INVOKABLE QString graceHint(int degree, int octave, bool sharp, int dur) const;
    Q_INVOKABLE void setGrace(int degree, int octave, bool sharp, int dur);
    Q_INVOKABLE void clearGrace();
    Q_INVOKABLE void clearAll();

    // ------------------------------------------------------------- 文件
    Q_INVOKABLE void refreshProjects();
    Q_INVOKABLE bool saveProject(const QString& name);
    Q_INVOKABLE bool openProject(const QString& name);
    /// 打开演奏谱：只能恢复音符，连音线/反复/转调/倚音在导出时已展开，恢复不了
    Q_INVOKABLE bool openDhs(const QString& name);
    /// 导出 `.dhs` 进曲谱目录，曲库刷新就能看到
    Q_INVOKABLE bool exportDhs(const QString& name);
    /// 关页面/崩溃前的自动保存，下次开程序接着改
    Q_INVOKABLE bool autosave();
    Q_INVOKABLE bool restoreAutosave();
    Q_INVOKABLE bool hasAutosave() const;
    /// 当前谱子导出的 `.dhs` 文本：试听把它交给 `Preview`，界面不自己算音高
    Q_INVOKABLE QString dhsText() const;

signals:
    void documentChanged();
    void cursChanged();
    void statusChanged();

private:
    [[nodiscard]] std::optional<int> selectedIndex() const;
    void setStatus(QString text);
    void noteAdded();

    Layout layout_;
    editor::Score score_;
    int selected_ = -1;
    int tieFrom_ = -1;
    QString openName_;
    QString status_;
    QStringList projects_;

    int curDur_ = 4;
    bool curDot_ = false;
    bool curSharp_ = false;
    int curOctave_ = 0;
};

}  // namespace harmonica::app
