#include "library.h"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include "score.h"

namespace harmonica::app {

std::vector<SongInfo> scanLibrary(const QString& dhsDir) {
    std::vector<SongInfo> out;
    const QDir dir{dhsDir};
    if (!dir.exists()) return out;

    const QStringList filters{QStringLiteral("*.dhs")};
    auto entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& info : entries) {
        SongInfo song;
        song.file = info.fileName();

        QFile file{info.absoluteFilePath()};
        if (!file.open(QIODevice::ReadOnly)) {
            song.error = QStringLiteral("读取失败：%1").arg(file.errorString());
            out.push_back(song);
            continue;
        }
        // BOM 由 parseScore 统一处理，这里不再各自剥一遍
        auto parsed = parseScore(QString::fromUtf8(file.readAll()));
        if (!parsed) {
            song.error = parsed.error();
            out.push_back(song);
            continue;
        }
        song.title = parsed->title;
        song.artist = parsed->artist;
        song.bpm = parsed->bpm;
        song.playsMs = parsed->durationMs();
        song.notes = parsed->noteCount();
        out.push_back(song);
    }
    return out;
}

QVariantList libraryAsVariantList(const std::vector<SongInfo>& songs) {
    QVariantList out;
    for (const SongInfo& song : songs) {
        out.push_back(QVariantMap{
            {QStringLiteral("file"), song.file},
            {QStringLiteral("title"), song.title},
            {QStringLiteral("artist"), song.artist},
            {QStringLiteral("bpm"), song.bpm},
            {QStringLiteral("playsMs"), song.playsMs},
            {QStringLiteral("notes"), static_cast<qlonglong>(song.notes)},
            {QStringLiteral("error"), song.error},
        });
    }
    return out;
}

}  // namespace harmonica::app
