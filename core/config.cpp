#include "config.h"

#include <algorithm>
#include <climits>
#include <unordered_map>

#include <QFile>
#include <QStringList>

namespace harmonica {
namespace {

const std::unordered_map<QString, std::uint16_t>& namedKeys() {
    static const std::unordered_map<QString, std::uint16_t> table{
        {QStringLiteral("COMMA"), 0xBC},      {QStringLiteral(","), 0xBC},
        {QStringLiteral("PERIOD"), 0xBE},     {QStringLiteral("."), 0xBE},
        {QStringLiteral("SEMICOLON"), 0xBA},  {QStringLiteral(";"), 0xBA},
        {QStringLiteral("SLASH"), 0xBF},      {QStringLiteral("/"), 0xBF},
        {QStringLiteral("SPACE"), 0x20},      {QStringLiteral("LBRACKET"), 0xDB},
        {QStringLiteral("RBRACKET"), 0xDD},   {QStringLiteral("MINUS"), 0xBD},
        {QStringLiteral("EQUAL"), 0xBB},
    };
    return table;
}

/// 只接受 ASCII 十进制 —— 和 `parse::<u64>()` 一样，`-5`、`1e3`、全角数字都算错。
/// 上限只管「塞得进 int」，界面那条 2000 毫秒的限制不在这里，手写文件不受它约束。
std::expected<int, QString> parseUnsigned(const QString& text) {
    const bool digitsOnly = !text.isEmpty() && std::all_of(text.begin(), text.end(), [](QChar c) {
                               return c.unicode() >= u'0' && c.unicode() <= u'9';
                           });
    bool ok = false;
    const qlonglong value = digitsOnly ? text.toLongLong(&ok) : -1;
    if (!digitsOnly || !ok || value > INT_MAX) {
        return std::unexpected(QStringLiteral("`%1` 不是合法整数").arg(text));
    }
    return static_cast<int>(value);
}

}  // namespace

std::expected<Config, QString> Config::load(const QString& path) {
    Config cfg;
    if (!QFile::exists(path)) return cfg;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(
            QStringLiteral("读取配置 %1 失败：%2").arg(path, file.errorString()));
    }
    if (auto applied = cfg.applyIni(QString::fromUtf8(file.readAll())); !applied) {
        return std::unexpected(applied.error());
    }
    return cfg;
}

std::expected<void, QString> Config::applyIni(const QString& text) {
    QString section;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        const QString line = lines.at(index).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.sliced(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) continue;

        const QString key = line.sliced(0, equals).trimmed().toLower();
        const QString value = line.sliced(equals + 1).trimmed();
        const QString where = QStringLiteral("第 %1 行：").arg(index + 1);

        if (section == QLatin1String("keys") && key.size() == 2 && key.startsWith(QLatin1Char('d'))) {
            auto position = parseUnsigned(key.sliced(1));
            if (!position) continue;
            const int degree = *position;
            if (degree >= 1 && degree <= 7) {
                auto parsed = parseKeyName(value);
                if (!parsed) return std::unexpected(where + parsed.error());
                degreeKeys[degree - 1] = *parsed;
            } else if (degree == 8) {
                auto parsed = parseKeyName(value);
                if (!parsed) return std::unexpected(where + parsed.error());
                highDoKey = *parsed;
            }
            continue;
        }
        if (section == QLatin1String("octave")
            && (key == QLatin1String("low") || key == QLatin1String("high")
                || key == QLatin1String("sharp"))) {
            auto parsed = parseButton(value);
            if (!parsed) return std::unexpected(where + parsed.error());
            std::optional<MouseButton>& target = key == QLatin1String("low")
                                                     ? lowButton
                                                     : (key == QLatin1String("high") ? highButton
                                                                                      : sharpButton);
            target = *parsed;
            continue;
        }
        if (section == QLatin1String("timing")
            && (key == QLatin1String("mouse_lead_ms") || key == QLatin1String("note_gap_ms"))) {
            auto parsed = parseUnsigned(value);
            if (!parsed) return std::unexpected(where + parsed.error());
            int& target = key == QLatin1String("mouse_lead_ms") ? timing.mouseLeadMs : timing.noteGapMs;
            target = *parsed;
        }
        // 其余一律安静忽略：已删除的保底项留在老配置文件里也不该报错
    }
    return {};
}

QString Config::toIni() const {
    const auto button = [](std::optional<MouseButton> b) {
        if (!b) return QStringLiteral("none");
        return buttonName(*b);
    };

    QString out;
    out += QStringLiteral("# 三角洲行动 口琴自动演奏 - 键位配置\n");
    out += QStringLiteral("# 以 # 或 ; 开头的是注释；改完存盘后重启程序生效。\n");
    out += QStringLiteral("\n[keys]\n");
    out += QStringLiteral("# 简谱 1..7 对应的按键（游戏口琴界面的默认布局）\n");
    for (int i = 0; i < 7; ++i) {
        out += QStringLiteral("d%1=%2\n").arg(i + 1).arg(keyName(degreeKeys[i]));
    }
    out += QStringLiteral(
        "# 第 8 键：高音 do。配合高音区修饰就是超高音 do（简谱 1 上面两个点）。\n"
        "# 可写：COMMA（逗号）| PERIOD（句点）| SLASH（斜杠）| 单个字母/数字\n");
    out += QStringLiteral("d8=%1\n").arg(keyName(highDoKey));
    out += QStringLiteral("\n[octave]\n");
    out += QStringLiteral("# 低音区 / 高音区 / 升半音 用哪个鼠标键作为修饰（按住式）\n");
    out += QStringLiteral("# 可选：left | middle | right | none\n");
    out += QStringLiteral("low=%1\nhigh=%2\nsharp=%3\n").arg(button(lowButton), button(highButton), button(sharpButton));
    out += QStringLiteral("\n[timing]\n");
    out += QStringLiteral(
        "# 修饰键提前量：降调/升调/半音这几个鼠标键，提前多少毫秒按下。\n"
        "# 游戏里修饰没生效、或者音区听起来不对，就把这个调大（比如 80）。\n");
    out += QStringLiteral("mouse_lead_ms=%1\n").arg(timing.mouseLeadMs);
    out += QStringLiteral(
        "\n# 松键间隔：松开一个音之后，隔多少毫秒才按下一个音。\n"
        "# 越小越紧凑；调大只是让节奏变拖，改善不了漏音（见 README「漏音排查」）。\n");
    out += QStringLiteral("note_gap_ms=%1\n").arg(timing.noteGapMs);
    return out;
}

QString Config::defaultIniText() { return Config{}.toIni(); }

std::pair<std::uint16_t, std::vector<MouseButton>> Config::resolve(const Pitch& pitch) const {
    // 超高音 do = 高音区修饰 + 第 8 键，比高音 do 再高八度
    if (pitch.register_ == Register::Top && pitch.degree == 1) {
        return {highDoKey, buttonsFor(Register::High, pitch.sharp)};
    }
    // 其余一律「音区修饰 + 度数键」，高音区保持右键 + Z..M 不变
    return {degreeKeys[pitch.degree - 1], buttonsFor(pitch.register_, pitch.sharp)};
}

std::vector<MouseButton> Config::buttonsFor(Register register_, bool sharp) const {
    std::vector<MouseButton> out;
    std::optional<MouseButton> registerButton;
    switch (register_) {
        case Register::Low: registerButton = lowButton; break;
        case Register::Mid: registerButton = std::nullopt; break;
        case Register::High: registerButton = highButton; break;
        // 超高音区同样是升八度修饰，只是音键换成第 8 键
        case Register::Top: registerButton = highButton; break;
    }
    if (registerButton) out.push_back(*registerButton);
    if (sharp && sharpButton) out.push_back(*sharpButton);
    return out;
}

std::vector<std::uint16_t> Config::allKeys() const {
    std::vector<std::uint16_t> keys(degreeKeys.begin(), degreeKeys.end());
    keys.push_back(highDoKey);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

std::expected<std::optional<MouseButton>, QString> parseButton(const QString& text) {
    const QString name = text.trimmed().toLower();
    if (name == QLatin1String("left")) return std::optional<MouseButton>{MouseButton::Left};
    if (name == QLatin1String("middle")) return std::optional<MouseButton>{MouseButton::Middle};
    if (name == QLatin1String("right")) return std::optional<MouseButton>{MouseButton::Right};
    if (name == QLatin1String("none")) return std::optional<MouseButton>{};
    return std::unexpected(
        QStringLiteral("未知鼠标键 `%1`，可选 left/middle/right/none").arg(text));
}

std::expected<std::uint16_t, QString> parseKeyName(const QString& text) {
    const QString upper = text.trimmed().toUpper();
    if (const auto found = namedKeys().find(upper); found != namedKeys().end()) {
        return found->second;
    }
    if (upper.size() == 1) {
        const QChar c = upper.at(0);
        const char16_t code = c.unicode();
        const bool asciiLetter = (code >= u'A' && code <= u'Z');
        const bool asciiDigit = (code >= u'0' && code <= u'9');
        if (asciiLetter || asciiDigit) return static_cast<std::uint16_t>(code);
    }
    return std::unexpected(
        QStringLiteral("无法识别的按键名 `%1`（可用：单个字母/数字、COMMA、PERIOD、SPACE 等）")
            .arg(text));
}

QString keyName(std::uint16_t code) {
    switch (code) {
        case 0xBC: return QStringLiteral("COMMA");
        case 0xBE: return QStringLiteral("PERIOD");
        case 0xBA: return QStringLiteral("SEMICOLON");
        case 0xBF: return QStringLiteral("SLASH");
        case 0x20: return QStringLiteral("SPACE");
        case 0xDB: return QStringLiteral("LBRACKET");
        case 0xDD: return QStringLiteral("RBRACKET");
        case 0xBD: return QStringLiteral("MINUS");
        case 0xBB: return QStringLiteral("EQUAL");
        default: break;
    }
    if (code < 0x80) {
        const char ascii = static_cast<char>(code);
        const bool graphic = ascii > 0x20 && ascii < 0x7F;
        if (graphic) return QString(QChar::fromLatin1(ascii));
    }
    return QStringLiteral("VK_%1").arg(code, 4, 16, QLatin1Char('0'));
}

QString buttonName(MouseButton button) {
    switch (button) {
        case MouseButton::Left: return QStringLiteral("left");
        case MouseButton::Middle: return QStringLiteral("middle");
        case MouseButton::Right: return QStringLiteral("right");
    }
    return QStringLiteral("left");
}

}  // namespace harmonica
