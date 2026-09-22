#include "score.h"

#include <algorithm>

#include <QStringList>

namespace harmonica {
namespace {

/// 只接受 ASCII 十进制位 —— QChar::isDigit 认阿拉伯-印度数字，那是谱面不想要的
bool asciiDigit(QChar c) { return c.unicode() >= u'0' && c.unicode() <= u'9'; }

/// 按任意空白切开，丢掉空段
QStringList splitWhitespace(QStringView line) {
    QStringList out;
    int start = 0;
    const int n = static_cast<int>(line.size());
    for (int i = 0; i <= n; ++i) {
        const bool isBoundary = (i == n) || line[i].isSpace();
        if (!isBoundary) continue;
        if (i > start) out.append(line.sliced(start, i - start).toString());
        start = i + 1;
    }
    return out;
}

std::expected<std::uint32_t, QString> parseTicks(QStringView text) {
    const QString digits = text.toString();
    const bool allDigits = std::all_of(digits.begin(), digits.end(), asciiDigit);
    bool ok = false;
    const uint value = allDigits ? digits.toUInt(&ok) : 0;
    if (!allDigits || !ok || value == 0) {
        return std::unexpected(QStringLiteral("tick 数不是正整数"));
    }
    return value;
}

std::expected<Event, QString> parseToken(QStringView token) {
    const QString shown = token.toString();
    const auto reject = [&shown](const QString& why) {
        return std::unexpected(QStringLiteral("`%1` %2").arg(shown, why));
    };

    const int slash = token.indexOf(QLatin1Char('/'));
    if (slash < 0) return reject(QStringLiteral("缺少 `/持续tick数`"));

    auto ticks = parseTicks(token.sliced(slash + 1));
    if (!ticks) return std::unexpected(ticks.error());

    const QString head = token.sliced(0, slash).toString().trimmed().toUpper();
    if (head == QLatin1String("R")) return RestEvent{*ticks};

    if (head.isEmpty()) return reject(QStringLiteral("音位必须以 L/M/H/T 开头，或用 R 表示休止"));

    Register register_ = Register::Mid;
    bool recognized = true;
    switch (head.at(0).toLatin1()) {
        case 'L': register_ = Register::Low; break;
        case 'M': register_ = Register::Mid; break;
        case 'H': register_ = Register::High; break;
        case 'T': register_ = Register::Top; break;
        default: recognized = false; break;
    }
    if (!recognized) return reject(QStringLiteral("音位必须以 L/M/H/T 开头，或用 R 表示休止"));
    if (head.size() < 2) return reject(QStringLiteral("音位缺少 1..7 的度数"));

    const QChar degreeChar = head.at(1);
    if (!asciiDigit(degreeChar)) return reject(QStringLiteral("音位缺少 1..7 的度数"));
    const auto degree = static_cast<std::uint8_t>(degreeChar.digitValue());
    if (degree < 1 || degree > 7) return reject(QStringLiteral("度数必须在 1..7 之间"));
    if (register_ == Register::Top && degree != 1) {
        return reject(QStringLiteral("超高音区只有 do，只能写成 T1"));
    }

    bool sharp = false;
    if (head.size() > 2) {
        if (head.at(2) != QLatin1Char('#')) {
            return reject(QStringLiteral("音位里有多余字符（升号请写在末尾）"));
        }
        sharp = true;
        // 升号之后不许再有别的东西
        if (head.size() > 3) {
            return reject(QStringLiteral("音位里有多余字符（升号请写在末尾）"));
        }
    }

    return NoteEvent{Pitch{register_, degree, sharp}, *ticks};
}

}  // namespace

std::uint32_t ticksOf(const Event& event) {
    return std::visit([](const auto& e) { return e.ticks; }, event);
}

QString pitchName(const Pitch& pitch) {
    const QChar r = [&] {
        switch (pitch.register_) {
            case Register::Low: return QLatin1Char('L');
            case Register::Mid: return QLatin1Char('M');
            case Register::High: return QLatin1Char('H');
            case Register::Top: return QLatin1Char('T');
        }
        return QLatin1Char('M');
    }();
    return QStringLiteral("%1%2%3")
        .arg(r)
        .arg(pitch.degree)
        .arg(pitch.sharp ? QStringLiteral("#") : QString());
}

double Song::msPerTick() const { return 60'000.0 / bpm / static_cast<double>(ticksPerBeat); }

std::uint32_t Song::totalTicks() const {
    std::uint32_t total = 0;
    for (const auto& event : events) total += ticksOf(event);
    return total;
}

double Song::durationMs() const { return static_cast<double>(totalTicks()) * msPerTick(); }

std::size_t Song::noteCount() const {
    std::size_t count = 0;
    for (const auto& event : events) {
        if (std::holds_alternative<NoteEvent>(event)) ++count;
    }
    return count;
}

std::expected<Song, QString> parseScore(const QString& text) {
    QString body = text;
    while (body.startsWith(QChar(0xFEFF))) body = body.sliced(1);

    Song song;
    song.title = QStringLiteral("未命名");
    bool inScore = false;

    const QStringList lines = body.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const int lineno = index + 1;
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.compare(QStringLiteral("[score]"), Qt::CaseInsensitive) == 0) {
            inScore = true;
            continue;
        }

        const QStringView view{line};
        if (!inScore) {
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals >= 0) {
                const QString key = line.sliced(0, equals).trimmed().toLower();
                const QString value = line.sliced(equals + 1).trimmed();
                if (key == QLatin1String("title")) {
                    song.title = value;
                } else if (key == QLatin1String("artist")) {
                    song.artist = value;
                } else if (key == QLatin1String("bpm")) {
                    bool ok = false;
                    const double parsed = value.toDouble(&ok);
                    if (!ok) {
                        return std::unexpected(QStringLiteral("第 %1 行：bpm 不是数字").arg(lineno));
                    }
                    song.bpm = parsed;
                } else if (key == QLatin1String("ticks_per_beat")) {
                    bool ok = false;
                    const uint parsed = value.toUInt(&ok);
                    if (!ok || parsed == 0) {
                        return std::unexpected(
                            QStringLiteral("第 %1 行：ticks_per_beat 不是正整数").arg(lineno));
                    }
                    song.ticksPerBeat = parsed;
                }
                continue;
            }
            // 没有 [score] 标记时，非 key=value 行也当作谱面
        }

        for (const QString& token : splitWhitespace(view)) {
            auto event = parseToken(QStringView{token});
            if (!event) {
                return std::unexpected(
                    QStringLiteral("第 %1 行：%2").arg(lineno).arg(event.error()));
            }
            song.events.push_back(*event);
        }
    }

    if (song.bpm <= 0.0) return std::unexpected(QStringLiteral("bpm 必须大于 0"));
    if (song.ticksPerBeat == 0) {
        return std::unexpected(QStringLiteral("ticks_per_beat 必须大于 0"));
    }
    if (song.events.empty()) {
        return std::unexpected(QStringLiteral("谱面为空，没有解析到任何音符"));
    }
    return song;
}

}  // namespace harmonica
