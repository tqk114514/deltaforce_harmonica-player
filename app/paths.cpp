#include "paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "config.h"

namespace harmonica::app {

QString baseDir() {
    const QString exe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
    return QFileInfo(exe).absolutePath();
}

Layout layoutFor(const QString& root) {
    const QDir dir{root};
    const QString songs = dir.filePath(QStringLiteral("songs"));
    return Layout{
        .root = root,
        .songsDir = songs,
        .dhsDir = QDir{songs}.filePath(QLatin1String{DHS_SUBDIR}),
        .notationDir = QDir{songs}.filePath(QLatin1String{NOTATION_SUBDIR}),
        .configFile = QDir{root}.filePath(QStringLiteral("harmonica.ini")),
    };
}

QString ensureDirs(const Layout& layout) {
    for (const QString& dir : {layout.songsDir, layout.dhsDir, layout.notationDir}) {
        if (QFileInfo::exists(dir)) continue;
        if (!QDir{}.mkpath(dir)) {
            return QStringLiteral("%1：建不出来（多半是没有写权限）").arg(dir);
        }
    }
    return {};
}

QString ensureConfigFile(const Layout& layout) {
    if (QFileInfo::exists(layout.configFile)) return {};

    QFile file{layout.configFile};
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QStringLiteral("写 %1 失败：%2").arg(layout.configFile, file.errorString());
    }
    file.write(harmonica::Config::defaultIniText().toUtf8());
    file.close();
    return {};
}

}  // namespace harmonica::app
