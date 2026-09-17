//! 配置：键位映射、鼠标奏法、时序档位。
//!
//! 配置文件是极简 INI（避免引入 toml/serde 依赖）。程序启动时读取同目录的
//! `harmonica.ini`，不存在则用下面的默认值。键位在游戏里实测对不上时，改这里即可。

use std::collections::HashMap;
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MouseButton {
    Left,
    Middle,
    Right,
}

/// 时序参数，单位毫秒。
///
/// 只有两项。早期版本还有一组「保底」（最短按住、自动顺延、同音放宽、修饰键提前松开、
/// 按键时长下限），2026-09-17 全部移除 —— 实测表明漏音不是时值不够，
/// 而是鼠标修饰键与游戏视角/移动冲突，保底解决不了它，只会让节奏变慢。
/// 现在完全按谱面时值发按键，不做任何等待或延长。
#[derive(Debug, Clone, Copy)]
pub struct Timing {
    /// 修饰键（降调/升调/半音）提前多少毫秒按下
    pub mouse_lead_ms: u64,
    /// 松开一个音之后，隔多少毫秒才按下一个音
    pub note_gap_ms: u64,
}

impl Default for Timing {
    fn default() -> Self {
        Timing {
            mouse_lead_ms: 60,
            note_gap_ms: 40,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Config {
    /// 简谱 1..7 对应的虚拟键码，默认 Z X C V B N M
    pub degree_keys: [u16; 7],
    /// 第 8 键：高音 do。配合高音区修饰就是超高音 do（简谱 1 上面两个点）。
    pub high_do_key: u16,
    /// 低音区的鼠标奏法
    pub low_button: Option<MouseButton>,
    /// 高音区的鼠标奏法
    pub high_button: Option<MouseButton>,
    /// 升半音的鼠标奏法
    pub sharp_button: Option<MouseButton>,
    pub timing: Timing,
}

impl Default for Config {
    fn default() -> Self {
        Config {
            degree_keys: [vk('Z'), vk('X'), vk('C'), vk('V'), vk('B'), vk('N'), vk('M')],
            high_do_key: HIGH_DO_KEY,
            low_button: Some(MouseButton::Left),
            high_button: Some(MouseButton::Right),
            sharp_button: Some(MouseButton::Middle),
            timing: Timing::default(),
        }
    }
}

/// 字母 -> 虚拟键码
pub const fn vk(c: char) -> u16 {
    c.to_ascii_uppercase() as u16
}

/// 第 8 键（高音 do）的默认值：逗号键。可在 harmonica.ini 的 [keys] d8= 里改。
pub const HIGH_DO_KEY: u16 = 0xBC; // VK_OEM_COMMA

impl Config {
    pub fn load(path: &Path) -> Result<Config, String> {
        let mut cfg = Config::default();
        if !path.exists() {
            return Ok(cfg);
        }
        let text = fs::read_to_string(path)
            .map_err(|e| format!("读取配置 {} 失败：{e}", path.display()))?;
        cfg.apply_ini(&text)?;
        Ok(cfg)
    }

    fn apply_ini(&mut self, text: &str) -> Result<(), String> {
        let mut section = String::new();
        for raw in text.lines() {
            let line = raw.trim();
            if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
                continue;
            }
            if let Some(name) = line.strip_prefix('[').and_then(|s| s.strip_suffix(']')) {
                section = name.trim().to_ascii_lowercase();
                continue;
            }
            let Some((key, value)) = line.split_once('=') else {
                continue;
            };
            let key = key.trim().to_ascii_lowercase();
            let value = value.trim();

            match (section.as_str(), key.as_str()) {
                ("keys", k) if k.len() == 2 && k.starts_with('d') => {
                    if let Some(d) = k[1..].parse::<usize>().ok() {
                        if (1..=7).contains(&d) {
                            self.degree_keys[d - 1] = parse_key_name(value)?;
                        } else if d == 8 {
                            self.high_do_key = parse_key_name(value)?;
                        }
                    }
                }
                ("octave", "low") => self.low_button = parse_button(value)?,
                ("octave", "high") => self.high_button = parse_button(value)?,
                ("octave", "sharp") => self.sharp_button = parse_button(value)?,
                ("timing", "mouse_lead_ms") => self.timing.mouse_lead_ms = parse_u64(value)?,
                ("timing", "note_gap_ms") => self.timing.note_gap_ms = parse_u64(value)?,
                _ => {}
            }
        }
        Ok(())
    }

    /// 把一个音位解析成（要按的键，需要按住的鼠标键）。
    /// 演奏和校准都走这里，保证两处行为一致。
    pub fn resolve(&self, pitch: crate::score::Pitch) -> (u16, Vec<MouseButton>) {
        match (pitch.register, pitch.degree) {
            // 超高音 do = 高音区修饰 + 第 8 键，比高音 do 再高八度
            (crate::score::Register::Top, 1) => (
                self.high_do_key,
                self.buttons_for(crate::score::Register::High, pitch.sharp),
            ),
            // 其余一律「音区修饰 + 度数键」，高音区保持右键 + Z..M 不变
            _ => (
                self.degree_keys[(pitch.degree - 1) as usize],
                self.buttons_for(pitch.register, pitch.sharp),
            ),
        }
    }

    /// 把音区/升号翻译成需要按住的鼠标键（可能为空）
    pub fn buttons_for(&self, register: crate::score::Register, sharp: bool) -> Vec<MouseButton> {
        let mut out = Vec::new();
        let reg_btn = match register {
            crate::score::Register::Low => self.low_button,
            crate::score::Register::Mid => None,
            crate::score::Register::High => self.high_button,
            // 超高音区同样是升八度修饰，只是音键换成第 8 键
            crate::score::Register::Top => self.high_button,
        };
        if let Some(b) = reg_btn {
            out.push(b);
        }
        if sharp {
            if let Some(b) = self.sharp_button {
                out.push(b);
            }
        }
        out
    }

    /// 该程序可能会按下的所有按键，用于退出/异常时统一释放
    pub fn all_keys(&self) -> Vec<u16> {
        let mut keys: Vec<u16> = self.degree_keys.to_vec();
        keys.push(self.high_do_key);
        keys.sort_unstable();
        keys.dedup();
        keys
    }

    /// 生成一份带注释的默认配置文件内容
    pub fn default_ini_text() -> String {
        let d = Config::default();
        let name = |v: u16| key_name(v);
        format!(
            "# 三角洲行动 口琴自动演奏 - 键位配置\n\
             # 以 # 或 ; 开头的是注释；改完存盘后重启程序生效。\n\
             \n\
             [keys]\n\
             # 简谱 1..7 对应的按键（游戏口琴界面的默认布局）\n\
             d1={}\n\
             d2={}\n\
             d3={}\n\
             d4={}\n\
             d5={}\n\
             d6={}\n\
             d7={}\n\
             # 第 8 键：高音 do。配合高音区修饰就是超高音 do（简谱 1 上面两个点）。\n\
             # 可写：COMMA（逗号）| PERIOD（句点）| SLASH（斜杠）| 单个字母/数字\n\
             d8={}\n\
             \n\
             [octave]\n\
             # 低音区 / 高音区 / 升半音 用哪个鼠标键作为修饰（按住式）\n\
             # 可选：left | middle | right | none\n\
             low=left\n\
             high=right\n\
             sharp=middle\n\
             \n\
             [timing]\n\
             # 修饰键提前量：降调/升调/半音这几个鼠标键，提前多少毫秒按下。\n\
             # 游戏里修饰没生效、或者音区听起来不对，就把这个调大（比如 80）。\n\
             mouse_lead_ms=60\n\
             \n\
             # 松键间隔：松开一个音之后，隔多少毫秒才按下一个音。\n\
             # 太小会让前后两个音粘连、漏音；一般 40 就够。\n\
             note_gap_ms=40\n",
            name(d.degree_keys[0]),
            name(d.degree_keys[1]),
            name(d.degree_keys[2]),
            name(d.degree_keys[3]),
            name(d.degree_keys[4]),
            name(d.degree_keys[5]),
            name(d.degree_keys[6]),
            name(d.high_do_key),
        )
    }
}

fn parse_u64(s: &str) -> Result<u64, String> {
    s.parse().map_err(|_| format!("`{s}` 不是合法整数"))
}

fn parse_button(s: &str) -> Result<Option<MouseButton>, String> {
    match s.to_ascii_lowercase().as_str() {
        "left" => Ok(Some(MouseButton::Left)),
        "middle" => Ok(Some(MouseButton::Middle)),
        "right" => Ok(Some(MouseButton::Right)),
        "none" => Ok(None),
        other => Err(format!("未知鼠标键 `{other}`，可选 left/middle/right/none")),
    }
}

fn parse_key_name(s: &str) -> Result<u16, String> {
    let upper = s.trim().to_ascii_uppercase();
    let named = HashMap::from([
        ("COMMA", 0xBCu16),
        (",", 0xBC),
        ("PERIOD", 0xBE),
        (".", 0xBE),
        ("SEMICOLON", 0xBA),
        (";", 0xBA),
        ("SLASH", 0xBF),
        ("/", 0xBF),
        ("SPACE", 0x20),
        ("LBRACKET", 0xDB),
        ("RBRACKET", 0xDD),
        ("MINUS", 0xBD),
        ("EQUAL", 0xBB),
    ]);
    if let Some(v) = named.get(upper.as_str()) {
        return Ok(*v);
    }
    if upper.len() == 1 {
        let c = upper.chars().next().unwrap();
        if c.is_ascii_uppercase() {
            return Ok(c as u16);
        }
        if c.is_ascii_digit() {
            return Ok(c as u16);
        }
    }
    Err(format!(
        "无法识别的按键名 `{s}`（可用：单个字母/数字、COMMA、PERIOD、SPACE 等）"
    ))
}

pub fn key_name(vk: u16) -> String {
    match vk {
        0xBC => "COMMA".to_string(),
        0xBE => "PERIOD".to_string(),
        0xBA => "SEMICOLON".to_string(),
        0xBF => "SLASH".to_string(),
        0x20 => "SPACE".to_string(),
        0xDB => "LBRACKET".to_string(),
        0xDD => "RBRACKET".to_string(),
        0xBD => "MINUS".to_string(),
        0xBB => "EQUAL".to_string(),
        v if v < 0x80 && (v as u8).is_ascii_graphic() => (v as u8 as char).to_string(),
        other => format!("VK_{other:#04X}"),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_keys_are_zxcvbnm() {
        let c = Config::default();
        assert_eq!(key_name(c.degree_keys[0]), "Z");
        assert_eq!(key_name(c.degree_keys[6]), "M");
    }

    #[test]
    fn sharp_combines_with_octave_modifier() {
        use crate::score::{Pitch, Register};
        let cfg = Config::default();

        // 低音区 + 半音 = 左键 + 中键 + Z
        let (vk, btns) = cfg.resolve(Pitch {
            register: Register::Low,
            degree: 1,
            sharp: true,
        });
        assert_eq!(vk, vk_char('Z'));
        assert!(btns.contains(&MouseButton::Left));
        assert!(btns.contains(&MouseButton::Middle));
        assert!(!btns.contains(&MouseButton::Right));

        // 高音区一律右键 + 度数键（H1 也是右键 + Z，不用第 8 键）
        let (vk, btns) = cfg.resolve(Pitch {
            register: Register::High,
            degree: 1,
            sharp: false,
        });
        assert_eq!(vk, vk_char('Z'));
        assert_eq!(btns, vec![MouseButton::Right]);

        // 高音区加半音再叠中键
        let (_, btns) = cfg.resolve(Pitch {
            register: Register::High,
            degree: 2,
            sharp: true,
        });
        assert!(btns.contains(&MouseButton::Right));
        assert!(btns.contains(&MouseButton::Middle));

        // 中音区不带升号时不按任何鼠标键
        let (_, btns) = cfg.resolve(Pitch {
            register: Register::Mid,
            degree: 1,
            sharp: false,
        });
        assert!(btns.is_empty());

        // 超高音 do = 第 8 键 + 右键
        let (vk, btns) = cfg.resolve(Pitch {
            register: Register::Top,
            degree: 1,
            sharp: false,
        });
        assert_eq!(vk, cfg.high_do_key);
        assert_eq!(btns, vec![MouseButton::Right]);
    }

    fn vk_char(c: char) -> u16 {
        vk(c)
    }

    #[test]
    fn ini_overrides() {
        let c = Config::default();
        let mut c2 = c.clone();
        c2.apply_ini(
            "[keys]\nd3=K\n[octave]\nlow=none\n[timing]\nmouse_lead_ms=20\nnote_gap_ms=55",
        )
        .unwrap();
        assert_eq!(c2.degree_keys[2], vk('K'));
        assert_eq!(c2.low_button, None);
        assert_eq!(c2.timing.mouse_lead_ms, 20);
        assert_eq!(c2.timing.note_gap_ms, 55);
    }

    /// 已删除的保底项还留在老配置文件里时，必须被安静忽略而不是报错
    #[test]
    fn removed_timing_keys_are_ignored() {
        let mut c = Config::default();
        c.apply_ini(
            "[timing]\nmin_hold_ms=90\nsame_note_ratio=0.667\nmodifier_release_lead_ms=5\n\
             min_key_hold_ms=1\nenforce_min_hold=true",
        )
        .unwrap();
        // 只认留下的两项，其余保持默认
        assert_eq!(c.timing.mouse_lead_ms, Timing::default().mouse_lead_ms);
        assert_eq!(c.timing.note_gap_ms, Timing::default().note_gap_ms);
    }
}
