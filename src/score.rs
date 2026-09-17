//! 曲谱解析。格式与社区 .dhs 一致：
//!
//! ```text
//! # 注释
//! title=小星星
//! bpm=96
//! ticks_per_beat=12
//!
//! [score]
//! M1/12 M1/12 M5/12 M5/12 M6/12 M6/12 M5/24
//! R/12 L5/6 H3#/3
//! ```
//!
//! 事件 = `音位/持续tick数`；音位为 `区+度数[#]`，区为 L(低) M(中) H(高)，
//! 度数 1..7，`#` 升半音，`R` 为休止。tick 时长 = 60000 / bpm / ticks_per_beat 毫秒。

use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Register {
    Low,
    Mid,
    High,
    /// 超高音区：只有 do（简谱里 1 上面两个点）
    Top,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Pitch {
    pub register: Register,
    /// 1..=7
    pub degree: u8,
    pub sharp: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Event {
    Note { pitch: Pitch, ticks: u32 },
    Rest { ticks: u32 },
}

impl Event {
    pub fn ticks(&self) -> u32 {
        match self {
            Event::Note { ticks, .. } => *ticks,
            Event::Rest { ticks } => *ticks,
        }
    }
}

#[derive(Debug, Clone)]
pub struct Song {
    pub title: String,
    pub artist: String,
    pub bpm: f64,
    pub ticks_per_beat: u32,
    pub events: Vec<Event>,
}

impl Song {
    pub fn ms_per_tick(&self) -> f64 {
        60_000.0 / self.bpm / self.ticks_per_beat as f64
    }

    pub fn total_ticks(&self) -> u32 {
        self.events.iter().map(Event::ticks).sum()
    }

    pub fn duration_ms(&self) -> f64 {
        self.total_ticks() as f64 * self.ms_per_tick()
    }

    pub fn note_count(&self) -> usize {
        self.events
            .iter()
            .filter(|e| matches!(e, Event::Note { .. }))
            .count()
    }
}

impl fmt::Display for Pitch {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let r = match self.register {
            Register::Low => 'L',
            Register::Mid => 'M',
            Register::High => 'H',
            Register::Top => 'T',
        };
        write!(f, "{}{}{}", r, self.degree, if self.sharp { "#" } else { "" })
    }
}

pub fn parse(text: &str) -> Result<Song, String> {
    let mut title = String::from("未命名");
    let mut artist = String::new();
    let mut bpm = 72.0_f64;
    let mut ticks_per_beat = 12_u32;

    let mut in_score = false;
    let mut events: Vec<Event> = Vec::new();

    for (lineno, raw) in text.lines().enumerate() {
        let line = raw.trim();
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if line.eq_ignore_ascii_case("[score]") {
            in_score = true;
            continue;
        }
        if !in_score {
            if let Some((k, v)) = line.split_once('=') {
                match k.trim().to_ascii_lowercase().as_str() {
                    "title" => title = v.trim().to_string(),
                    "artist" => artist = v.trim().to_string(),
                    "bpm" => {
                        bpm = v
                            .trim()
                            .parse()
                            .map_err(|_| format!("第 {} 行：bpm 不是数字", lineno + 1))?;
                    }
                    "ticks_per_beat" => {
                        ticks_per_beat = v.trim().parse().map_err(|_| {
                            format!("第 {} 行：ticks_per_beat 不是正整数", lineno + 1)
                        })?;
                    }
                    _ => {}
                }
                continue;
            }
            // 没有 [score] 标记时，非 key=value 行也当作谱面
        }

        for token in line.split_whitespace() {
            events.push(parse_token(token, lineno + 1)?);
        }
    }

    if bpm <= 0.0 {
        return Err("bpm 必须大于 0".to_string());
    }
    if ticks_per_beat == 0 {
        return Err("ticks_per_beat 必须大于 0".to_string());
    }
    if events.is_empty() {
        return Err("谱面为空，没有解析到任何音符".to_string());
    }

    Ok(Song {
        title,
        artist,
        bpm,
        ticks_per_beat,
        events,
    })
}

fn parse_token(token: &str, lineno: usize) -> Result<Event, String> {
    let bad = |msg: String| Err(format!("第 {lineno} 行：`{token}` {msg}"));
    let Some((head, ticks_str)) = token.split_once('/') else {
        return bad("缺少 `/持续tick数`".to_string());
    };
    let ticks: u32 = ticks_str
        .parse()
        .map_err(|_| format!("第 {lineno} 行：`{token}` 的 tick 数不是正整数"))?;
    if ticks == 0 {
        return bad("tick 数必须大于 0".to_string());
    }

    let head = head.trim().to_ascii_uppercase();
    if head == "R" {
        return Ok(Event::Rest { ticks });
    }

    let mut chars = head.chars();
    let register = match chars.next() {
        Some('L') => Register::Low,
        Some('M') => Register::Mid,
        Some('H') => Register::High,
        Some('T') => Register::Top,
        _ => return bad("音位必须以 L/M/H/T 开头，或用 R 表示休止".to_string()),
    };
    let degree = match chars.next() {
        Some(c) if c.is_ascii_digit() => c.to_digit(10).unwrap() as u8,
        _ => return bad("音位缺少 1..7 的度数".to_string()),
    };
    if !(1..=7).contains(&degree) {
        return bad("度数必须在 1..7 之间".to_string());
    }
    if register == Register::Top && degree != 1 {
        return bad("超高音区只有 do，只能写成 T1".to_string());
    }
    let sharp = match chars.next() {
        Some('#') => true,
        Some(_) => return bad("音位里有多余字符（升号请写在末尾）".to_string()),
        None => false,
    };

    Ok(Event::Note {
        pitch: Pitch {
            register,
            degree,
            sharp,
        },
        ticks,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_header_and_notes() {
        let song = parse(
            "title=小星星\nbpm=96\nticks_per_beat=12\n[score]\nM1/12 M5/6 R/6 H3#/3",
        )
        .unwrap();
        assert_eq!(song.title, "小星星");
        assert_eq!(song.bpm, 96.0);
        assert_eq!(song.events.len(), 4);
        assert_eq!(
            song.events[3],
            Event::Note {
                pitch: Pitch {
                    register: Register::High,
                    degree: 3,
                    sharp: true
                },
                ticks: 3
            }
        );
        assert_eq!(song.note_count(), 3);
    }

    #[test]
    fn rejects_bad_tokens() {
        assert!(parse("[score]\nX1/12").is_err());
        assert!(parse("[score]\nM9/12").is_err());
        assert!(parse("[score]\nM1").is_err());
        assert!(parse("[score]\nM1/0").is_err());
    }

    #[test]
    fn top_register_only_allows_do() {
        assert!(parse("[score]\nT1/12").is_ok());
        // 超高音区只有 do
        assert!(parse("[score]\nT2/12").is_err());
        let song = parse("[score]\nT1#/12").unwrap();
        assert_eq!(
            song.events[0],
            Event::Note {
                pitch: Pitch {
                    register: Register::Top,
                    degree: 1,
                    sharp: true
                },
                ticks: 12
            }
        );
    }

    #[test]
    fn tick_timing() {
        let song = parse("bpm=120\nticks_per_beat=12\n[score]\nM1/12").unwrap();
        // 120 BPM 下每拍 500ms，一拍 12 tick => 每 tick 41.67ms
        assert!((song.ms_per_tick() - 41.6667).abs() < 0.01);
        assert!((song.duration_ms() - 500.0).abs() < 0.01);
    }
}
