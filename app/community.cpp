#include "community.h"

#include <algorithm>
#include <functional>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>

#include "config.h"
#include "preview.h"
#include "score.h"

namespace harmonica::app {
namespace {

/// 清单地址。谱子的相对路径也以它为基准。
const QUrl MANIFEST_URL{QStringLiteral("https://harmonica-songs.pages.dev/manifest.json")};

/// 校验不过就重来一次，重来还是不过才报「下载错误」
constexpr int DOWNLOAD_ATTEMPTS = 2;

QString fieldOf(const QJsonObject& object, const char* key) {
    return object.value(QLatin1String(key)).toString().trimmed();
}

QString normalizedSha(const QJsonObject& object, const char* key) {
    return fieldOf(object, key).toLower();
}

QUrl resolved(const QUrl& base, const QString& path) {
    return path.isEmpty() ? QUrl{} : base.resolved(QUrl{path});
}

/// 取地址最后一段当文件名
QString fileNameOf(const QUrl& url) {
    const QString path = url.path();
    return QUrl::fromPercentEncoding(path.section(QLatin1Char('/'), -1).toUtf8());
}

/// 只接受纯粹的文件名。清单是从网上拉下来的，不能让它决定往哪儿写。
bool isPlainFileName(const QString& name) {
    return !name.isEmpty() && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
           && !name.contains(QLatin1Char(':')) && !name.contains(QStringLiteral(".."));
}

}  // namespace

bool sha256Matches(const QByteArray& bytes, const QString& expectedHex) {
    if (expectedHex.isEmpty()) return false;
    const QByteArray digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(digest) == expectedHex.toLower();
}

QVariantList communityAsVariantList(const std::vector<CommunityEntry>& entries) {
    QVariantList out;
    for (const CommunityEntry& entry : entries) {
        out.push_back(QVariantMap{
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("dhsUrl"), entry.dhsUrl},
            {QStringLiteral("nmnUrl"), entry.nmnUrl},
            {QStringLiteral("playsMs"), entry.playsMs},
            {QStringLiteral("hasDuration"), entry.hasDuration},
            {QStringLiteral("downloadable"), entry.error.isEmpty() && !entry.dhsSha.isEmpty()
                                                && !entry.nmnSha.isEmpty()},
            {QStringLiteral("error"), entry.error},
        });
    }
    return out;
}

CommunityManager::CommunityManager(Layout layout, QObject* parent)
    : QObject{parent}, layout_{std::move(layout)}, network_{new QNetworkAccessManager{this}} {}

void CommunityManager::setStatus(QString text) {
    status_ = std::move(text);
    emit statusChanged();
}

void CommunityManager::get(const QUrl& url, std::function<void(QByteArray, QString)> done) {
    if (!url.isValid()) {
        done({}, QStringLiteral("地址不合法"));
        return;
    }
    ++busy_;
    emit busyChanged();

    QNetworkRequest request{url};
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    QNetworkReply* reply = network_->get(request);
    QPointer<CommunityManager> self{this};
    QObject::connect(reply, &QNetworkReply::finished, this, [self, reply, done = std::move(done)] {
        if (!self) {
            reply->deleteLater();
            return;
        }
        const QByteArray body = reply->readAll();
        const QString error =
            reply->error() == QNetworkReply::NoError ? QString() : reply->errorString();
        reply->deleteLater();
        --self->busy_;
        emit self->busyChanged();
        done(body, error);
    });
}

void CommunityManager::refresh() {
    setStatus(QStringLiteral("正在拉清单…"));
    QPointer<CommunityManager> self{this};
    get(MANIFEST_URL, [self](QByteArray body, QString error) {
        if (!self) return;
        if (!error.isEmpty()) {
            self->setStatus(QStringLiteral("拉不到清单：%1").arg(error));
            return;
        }
        self->entries_ = parseCommunityManifest(body, MANIFEST_URL);
        emit self->songsChanged();
        if (self->entries_.empty()) {
            self->setStatus(QStringLiteral("社区还没有曲谱"));
            return;
        }

        // 时长不是清单里的字段：把每份 .dhs 读进内存，交给 core 里那一个解析器算
        self->setStatus(QStringLiteral("正在读时长…"));
        int pending = 0;
        for (const CommunityEntry& entry : self->entries_) {
            if (entry.error.isEmpty() && !entry.dhsUrl.isEmpty()) ++pending;
        }
        if (pending == 0) {
            self->setStatus(QString());
            return;
        }
        auto remaining = std::make_shared<int>(pending);
        ++self->busy_;
        emit self->busyChanged();

        for (std::size_t i = 0; i < self->entries_.size(); ++i) {
            const QUrl url{self->entries_[i].dhsUrl};
            if (!url.isValid()) continue;
            const std::size_t row = i;
            self->get(url, [self, row, remaining](QByteArray body, QString error) {
                if (!self) return;
                CommunityEntry& entry = self->entries_[row];
                if (error.isEmpty()) {
                    entry.text = QString::fromUtf8(body);
                    const auto song = parseScore(entry.text);   // BOM 由 core 处理
                    if (song) {
                        entry.playsMs = song->durationMs();
                        entry.hasDuration = true;
                    } else {
                        entry.error = song.error();
                    }
                } else {
                    entry.error = error;
                }
                emit self->songsChanged();
                if (--(*remaining) == 0) {
                    self->setStatus(QString());
                    --self->busy_;
                    emit self->busyChanged();
                }
            });
        }
    });
}

bool CommunityManager::hasLocal(const QString& title) const {
    for (const CommunityEntry& entry : entries_) {
        if (entry.title == title) {
            const QString name = fileNameOf(QUrl{entry.dhsUrl});
            return isPlainFileName(name) && QFileInfo::exists(QDir{layout_.dhsDir}.filePath(name));
        }
    }
    return false;
}

QString CommunityManager::textOf(const QString& title) const {
    for (const CommunityEntry& entry : entries_) {
        if (entry.title == title) return entry.text;
    }
    return {};
}

QVariantList CommunityManager::songs() const {
    return communityAsVariantList(entries_);
}

void CommunityManager::download(const QString& title) {
    int row = -1;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].title == title) row = static_cast<int>(i);
    }
    if (row < 0) {
        emit downloadFailed(title, QStringLiteral("清单里没有这首"));
        return;
    }
    const CommunityEntry entry = entries_[row];
    if (!entry.error.isEmpty()) {
        emit downloadFailed(title, entry.error);
        return;
    }
    // 来历不明的东西不往盘上写
    if (entry.dhsSha.isEmpty() || entry.nmnSha.isEmpty()) {
        emit downloadFailed(title, QStringLiteral("清单里没有 sha256，无法校验，不下载"));
        return;
    }
    const QString dhsName = fileNameOf(QUrl{entry.dhsUrl});
    const QString nmnName = fileNameOf(QUrl{entry.nmnUrl});
    if (!isPlainFileName(dhsName) || !isPlainFileName(nmnName)) {
        emit downloadFailed(title, QStringLiteral("清单给了不合法的文件名"));
        return;
    }

    struct Attempt {
        QByteArray dhs;
        QByteArray nmn;
        bool dhsOk = false;
        bool nmnOk = false;
    };
    auto state = std::make_shared<Attempt>();
    QPointer<CommunityManager> self{this};

    auto writeBoth = [self, title, dhsName, nmnName, state](const QString& reason) {
        if (!self) return;
        if (!reason.isEmpty()) {
            emit self->downloadFailed(title, reason);
            self->setStatus(QString());
            return;
        }
        const QString dirError = ensureDirs(self->layout_);
        if (!dirError.isEmpty()) {
            emit self->downloadFailed(title, QStringLiteral("曲谱目录不可用：%1").arg(dirError));
            self->setStatus(QString());
            return;
        }
        const QString dhsPath = QDir{self->layout_.dhsDir}.filePath(dhsName);
        const QString nmnPath = QDir{self->layout_.notationDir}.filePath(nmnName);

        QFile dhs{dhsPath};
        if (!dhs.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit self->downloadFailed(title, QStringLiteral("写入 %1 失败：%2").arg(dhsPath, dhs.errorString()));
            self->setStatus(QString());
            return;
        }
        dhs.write(state->dhs);
        dhs.close();

        QFile nmn{nmnPath};
        if (!nmn.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString why = QStringLiteral("写入 %1 失败：%2").arg(nmnPath, nmn.errorString());
            nmn.close();
            QFile::remove(dhsPath);   // 不留半份
            emit self->downloadFailed(title, why);
            self->setStatus(QString());
            return;
        }
        nmn.write(state->nmn);
        nmn.close();

        self->setStatus(QStringLiteral("已下载 %1").arg(title));
        emit self->downloadDone(title);
    };

    // 两个文件各自最多试 DOWNLOAD_ATTEMPTS 次；任何一个失败都不落盘
    auto tryFetch = std::make_shared<std::function<void(QUrl, QString, int, bool)>>();
    *tryFetch = [self, title, state, writeBoth, tryFetch](QUrl url, QString expected, int attemptsLeft,
                                                         bool isDhs) {
        if (!self) return;
        self->get(url, [self, title, state, writeBoth, tryFetch, url, expected, attemptsLeft,
                        isDhs](QByteArray body, QString error) {
            if (!self) return;
            // 算的是原始字节：先按文本解码再编码回去，算出来的是「文本的哈希」
            const bool matches = error.isEmpty() && sha256Matches(body, expected);
            if (matches) {
                if (isDhs) {
                    state->dhs = body;
                    state->dhsOk = true;
                } else {
                    state->nmn = body;
                    state->nmnOk = true;
                }
                if (state->dhsOk && state->nmnOk) writeBoth(QString());
                return;
            }
            const QString reason =
                error.isEmpty() ? QStringLiteral("sha256 不一致") : QStringLiteral("网络出错：%1").arg(error);
            if (attemptsLeft > 1) {
                (*tryFetch)(url, expected, attemptsLeft - 1, isDhs);
                return;
            }
            writeBoth(QStringLiteral("%1：%2").arg(isDhs ? QStringLiteral("演奏谱")
                                                         : QStringLiteral("简谱工程"), reason));
        });
    };

    setStatus(QStringLiteral("正在下载 %1…").arg(title));
    (*tryFetch)(QUrl{entry.dhsUrl}, entry.dhsSha, DOWNLOAD_ATTEMPTS, true);
    (*tryFetch)(QUrl{entry.nmnUrl}, entry.nmnSha, DOWNLOAD_ATTEMPTS, false);
}

std::vector<CommunityEntry> parseCommunityManifest(const QByteArray& json, const QUrl& base) {
    std::vector<CommunityEntry> out;
    const auto parsed = QJsonDocument::fromJson(json);
    if (!parsed.isArray()) {
        out.push_back(CommunityEntry{{}, {}, {}, {}, {}, {}, 0.0, false,
                                      QStringLiteral("清单不是数组")});
        return out;
    }

    for (const QJsonValue& value : parsed.array()) {
        if (!value.isObject()) continue;
        const QJsonObject object = value.toObject();
        CommunityEntry entry;
        entry.title = fieldOf(object, "song_title");
        const QString dhsPath = fieldOf(object, "dhs_URL");
        const QString nmnPath = fieldOf(object, "nmn_URL");
        entry.dhsUrl = resolved(base, dhsPath).toString();
        entry.nmnUrl = resolved(base, nmnPath).toString();
        entry.dhsSha = normalizedSha(object, "dhs_sha256");
        entry.nmnSha = normalizedSha(object, "nmn_sha256");
        if (entry.dhsUrl.isEmpty()) entry.error = QStringLiteral("清单里没有 dhs_URL");
        else if (entry.dhsSha.isEmpty() || entry.nmnSha.isEmpty())
            entry.error = QStringLiteral("清单里没有 sha256，不下载");
        if (!entry.title.isEmpty() || !entry.error.isEmpty()) out.push_back(entry);
    }
    std::sort(out.begin(), out.end(), [](const CommunityEntry& a, const CommunityEntry& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return out;
}

}  // namespace harmonica::app
