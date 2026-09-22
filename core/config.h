#pragma once

//! 配置：键位映射、鼠标奏法、时序参数。
//!
//! 配置文件是极简 INI（不引入注册表或 JSON）。程序启动时读程序同目录的
//! `harmonica.ini`，不存在则按下面的默认值。键位在游戏里实测对不上时改这里即可。

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <vector>

#include <QString>

#include "score.h"

namespace harmonica {

/// 音区修饰用的鼠标键（按住式）。和托盘图标那个「左键/右键」不是一回事。
enum class MouseButton {
    Left,
    Middle,
    Right,
};

/// 时序参数，单位毫秒。
///
/// 只有两项。早期版本还有一组「保底」（最短按住、自动顺延、同音放宽、修饰键提前松开、
/// 按键时长下限），2026-09-17 全部移除 —— 实测表明漏音不是时值不够，
/// 而是游戏自己的问题：按键按下去了它也不出声（见 README「漏音排查」），
/// 保底解决不了它，只会让节奏变慢。现在完全按谱面时值发按键，不做任何等待或延长。
struct Timing {
    /// 修饰键（降调/升调/半音）提前多少毫秒按下
    int mouseLeadMs = 60;
    /// 松开一个音之后，隔多少毫秒才按下一个音
    int noteGapMs = 50;

    friend bool operator==(const Timing&, const Timing&) = default;
};

/// 字母 -> 虚拟键码（和 Rust 的 `c.to_ascii_uppercase() as u16` 一致）
constexpr std::uint16_t vk(char c) {
    return static_cast<std::uint16_t>(c >= 'a' && c <= 'z' ? c - 32 : c);
}

/// 第 8 键（高音 do）的默认值：逗号键。可在 harmonica.ini 的 [keys] d8= 里改。
inline constexpr std::uint16_t HIGH_DO_KEY = 0xBC;  // VK_OEM_COMMA

struct Config {
    /// 简谱 1..7 对应的虚拟键码，默认 Z X C V B N M
    std::array<std::uint16_t, 7> degreeKeys{
        vk('Z'), vk('X'), vk('C'), vk('V'), vk('B'), vk('N'), vk('M')};
    /// 第 8 键：高音 do。配合高音区修饰就是超高音 do（简谱 1 上面两个点）。
    std::uint16_t highDoKey = HIGH_DO_KEY;
    /// 低音区的鼠标奏法，`none` 时为 nullopt
    std::optional<MouseButton> lowButton = MouseButton::Left;
    /// 高音区的鼠标奏法
    std::optional<MouseButton> highButton = MouseButton::Right;
    /// 升半音的鼠标奏法
    std::optional<MouseButton> sharpButton = MouseButton::Middle;
    Timing timing;

    friend bool operator==(const Config&, const Config&) = default;

    /// 读配置文件。文件不存在时返回默认配置（不算错误）。
    static std::expected<Config, QString> load(const QString& path);

    /// 解析 INI 文本，把值覆盖到当前配置上。未知项安静忽略，
    /// 但**认得的项写错了要报错** —— 静默吞掉只会让用户以为设置生效了。
    std::expected<void, QString> applyIni(const QString& text);

    /// 生成配置文件内容（带注释）。
    ///
    /// 和 [`Config::applyIni`] 是一对：`load(toIni())` 必须还原出同一个配置，
    /// 单测盯着这条不变量。界面上改完设置写回文件走的就是这里。
    [[nodiscard]] QString toIni() const;

    /// 默认配置文件的内容
    [[nodiscard]] static QString defaultIniText();

    /// 把一个音位解析成（要按的键，需要按住的鼠标键）。
    /// 演奏和校准都走这里，保证两处行为一致。
    [[nodiscard]] std::pair<std::uint16_t, std::vector<MouseButton>> resolve(const Pitch& pitch) const;

    /// 把音区/升号翻译成需要按住的鼠标键（可能为空）
    [[nodiscard]] std::vector<MouseButton> buttonsFor(Register register_, bool sharp) const;

    /// 该程序可能会按下的所有按键，用于退出/异常时统一释放
    [[nodiscard]] std::vector<std::uint16_t> allKeys() const;
};

/// 解析鼠标键名。`none` 表示不用这个修饰（返回 nullopt）。
///
/// 界面上改设置时也要走这里校验，别在外面另写一套 —— 配置文件里能写什么、
/// 界面上就能填什么，规则只有这一份。
std::expected<std::optional<MouseButton>, QString> parseButton(const QString& text);

/// 解析按键名。规则和 `keyName` 的输出互为逆运算，配置文件与设置界面共用。
std::expected<std::uint16_t, QString> parseKeyName(const QString& text);

/// 虚拟键码 -> 配置文件里那种名字
QString keyName(std::uint16_t code);

/// 鼠标键的机器名（left / middle / right），和 harmonica.ini 里写的一致。
/// 干跑日志和校准界面都用它。
QString buttonName(MouseButton button);

}  // namespace harmonica
