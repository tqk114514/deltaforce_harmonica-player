//! 时序调度：把曲谱展开成带绝对时间戳的动作表，再按表执行。
//!
//! 设计要点：
//! 1. 时间戳全部相对演奏起点计算（绝对时间轴），不累加 sleep 误差；
//! 2. 相邻同修饰的音符合并修饰键区间，减少鼠标事件；
//! 3. 完全按谱面时值发按键，不做顺延或延长；
//! 4. 任何退出路径都释放按键。

use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use crate::config::{Config, MouseButton, key_name};
use crate::input;
use crate::score::{Event, Pitch, Register, Song};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    KeyDown(u16),
    KeyUp(u16),
    MouseDown(MouseButton),
    MouseUp(MouseButton),
}

#[derive(Debug, Clone, Copy)]
pub struct TimedAction {
    pub at_ms: f64,
    pub action: Action,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlayState {
    Running,
    Stopped,
}

pub type Control = Arc<Mutex<PlayState>>;

struct Note {
    start_ms: f64,
    dur_ms: f64,
    vk: u16,
    buttons: Vec<MouseButton>,
}

/// 音区整体平移（半音阶十二度之外的粗移调）：-1 低八度，+1 高八度
fn shift_register(reg: Register, shift: i32) -> Register {
    let idx = match reg {
        Register::Low => 0,
        Register::Mid => 1,
        Register::High => 2,
        Register::Top => 3,
    } + shift;
    match idx.clamp(0, 3) {
        0 => Register::Low,
        1 => Register::Mid,
        2 => Register::High,
        _ => Register::Top,
    }
}

/// 按键时长的数学下限（毫秒）。时值比 `note_gap_ms` 还短的音会被压到这里，
/// 保证「按下」之后一定有对应的「松开」—— 否则键会卡在按下状态。
/// 这不是保底，是防止动作表出现「先松后按」的硬约束。
const MIN_KEY_HOLD_MS: f64 = 1.0;

/// 把曲谱展开成动作表。
///
/// 完全按谱面时值来：不缩短、不顺延、不延长。只有两类顺序约束会推迟动作 ——
/// 修饰键的切换必须等前一个音彻底松开，段首音符必须等修饰键建立起来，
/// 否则会发出滑音或整段跑调。这两条不改变总时长，只决定先后。
pub fn build_actions(song: &Song, cfg: &Config, shift: i32) -> Vec<TimedAction> {
    let ms_per_tick = song.ms_per_tick();
    let mut notes: Vec<Note> = Vec::new();
    // 时间轴整体后移一个 lead，保证第一个音的鼠标修饰键也能提前按下
    let mut cursor = cfg.timing.mouse_lead_ms as f64;

    for ev in &song.events {
        let dur = ev.ticks() as f64 * ms_per_tick;
        if let Event::Note { pitch, .. } = ev {
            let p = Pitch {
                register: shift_register(pitch.register, shift),
                degree: pitch.degree,
                sharp: pitch.sharp,
            };
            let (vk, buttons) = cfg.resolve(p);
            notes.push(Note {
                start_ms: cursor,
                dur_ms: dur,
                vk,
                buttons,
            });
        }
        cursor += dur;
    }

    let lead = cfg.timing.mouse_lead_ms as f64;
    let gap = cfg.timing.note_gap_ms as f64;

    let mut actions: Vec<TimedAction> = Vec::new();
    // 修饰键按"差集"维护：本段仍然需要的键保持按住不撒手，
    // 只松开不再需要的、按下新需要的。
    let mut held: Vec<MouseButton> = Vec::new();
    let mut last_off = 0.0_f64;
    // 上一段最后一个音符键的松开时刻。这一段修饰键要动，必须等它过去。
    let mut prev_last_key_up = 0.0_f64;
    let mut i = 0;

    while i < notes.len() {
        let btns = notes[i].buttons.clone();
        let mut j = i;
        while j + 1 < notes.len() && notes[j + 1].buttons == btns {
            j += 1;
        }

        // 修饰键最早能动的时刻：前一个音彻底结束，再留 gap 的余量。
        // 早了的话，音符还在响、音高就变了，会发出一个很难听的滑音。
        let earliest = if i > 0 { prev_last_key_up + gap } else { 0.0 };
        let on_at = (notes[i].start_ms - lead).max(earliest);

        // 先松开本段用不到的，再按下本段新增的。
        // 两者同刻时靠 order() 保证「先按新的、再松旧的」，不会瞬间串音。
        for b in held.iter().copied() {
            if !btns.contains(&b) {
                actions.push(TimedAction {
                    at_ms: on_at,
                    action: Action::MouseUp(b),
                });
            }
        }
        for b in btns.iter().copied() {
            if !held.contains(&b) {
                actions.push(TimedAction {
                    at_ms: on_at,
                    action: Action::MouseDown(b),
                });
            }
        }
        held = btns.clone();

        // 音符键。段首那一个必须等修饰键建立起来（on_at + lead），
        // 否则开头一小段是按在错误的音区上，听起来就是跑调。
        let mut last_key_up = 0.0_f64;
        for idx in i..=j {
            let n = &notes[idx];
            let down_at = if idx == i {
                n.start_ms.max(on_at + lead)
            } else {
                n.start_ms
            };
            // 完全按谱面时值：按住 = 时值 - 松键间隔
            let up_at = down_at + (n.dur_ms - gap).max(MIN_KEY_HOLD_MS);

            actions.push(TimedAction {
                at_ms: down_at,
                action: Action::KeyDown(n.vk),
            });
            actions.push(TimedAction {
                at_ms: up_at,
                action: Action::KeyUp(n.vk),
            });
            last_key_up = up_at;
        }
        prev_last_key_up = last_key_up;

        // 收尾松开修饰键，同样要等本段最后一个音真的松开之后
        last_off = (notes[j].start_ms + notes[j].dur_ms + lead).max(last_key_up + gap);
        i = j + 1;
    }

    // 收尾：松开还按着的修饰键
    for b in held {
        actions.push(TimedAction {
            at_ms: last_off,
            action: Action::MouseUp(b),
        });
    }

    actions.sort_by(|a, b| {
        a.at_ms
            .total_cmp(&b.at_ms)
            .then_with(|| order(a.action).cmp(&order(b.action)))
    });
    actions
}

/// 同一时刻的执行顺序。
/// 松开音符键排最前：先掐断上一个音，再建立下一个音的修饰，避免瞬间串音。
/// 松开修饰键排最后：保证修饰在音符键松开之后才释放。
fn order(a: Action) -> u8 {
    match a {
        Action::KeyUp(_) => 0,
        Action::MouseDown(_) => 1,
        Action::KeyDown(_) => 2,
        Action::MouseUp(_) => 3,
    }
}

/// 按动作表演奏。dry_run 时只打印不发送输入。
/// progress 用于把当前演奏位置回传给界面（毫秒）。
pub fn play(
    actions: &[TimedAction],
    cfg: &Config,
    control: Control,
    dry_run: bool,
    progress: Option<&std::sync::atomic::AtomicU64>,
) -> Result<(), String> {
    // 无论正常结束、提前 return 还是 panic，离开作用域时都会松开所有按键。
    // 漏掉这一步的后果很严重：鼠标键会卡在按下状态，导致整个系统点不动东西。
    let keys = cfg.all_keys();
    let _guard = ReleaseGuard {
        keys: &keys,
        active: !dry_run,
    };

    let base = Instant::now();
    let mut idx = 0_usize;

    loop {
        if *control.lock().map_err(|_| "控制状态锁已损坏")? == PlayState::Stopped {
            break;
        }

        if idx >= actions.len() {
            break;
        }

        let target = base + Duration::from_secs_f64(actions[idx].at_ms / 1000.0);
        let now = Instant::now();
        if now < target {
            let remain = target - now;
            thread::sleep(remain.min(Duration::from_millis(20)));
            continue;
        }

        let ta = &actions[idx];
        if dry_run {
            println!("  [{:8.0} ms] {}", ta.at_ms, describe(ta.action));
        } else {
            dispatch(ta.action);
        }
        if let Some(p) = progress {
            p.store(ta.at_ms as u64, std::sync::atomic::Ordering::Relaxed);
        }
        idx += 1;
    }

    Ok(())
}

/// 作用域结束时松开所有按键，panic 展开时同样生效。
pub struct ReleaseGuard<'a> {
    keys: &'a [u16],
    /// dry-run 不发送任何输入，这里也要跟着关掉
    active: bool,
}

impl<'a> ReleaseGuard<'a> {
    pub fn new(keys: &'a [u16], active: bool) -> Self {
        ReleaseGuard { keys, active }
    }
}

impl Drop for ReleaseGuard<'_> {
    fn drop(&mut self) {
        if self.active {
            input::release_all(self.keys);
        }
    }
}

pub fn dispatch(action: Action) {
    match action {
        Action::KeyDown(vk) => {
            input::key_down(vk);
        }
        Action::KeyUp(vk) => {
            input::key_up(vk);
        }
        Action::MouseDown(b) => {
            input::mouse_down(b);
        }
        Action::MouseUp(b) => {
            input::mouse_up(b);
        }
    }
}

pub fn describe(action: Action) -> String {
    match action {
        Action::KeyDown(vk) => format!("按下 {}", key_name(vk)),
        Action::KeyUp(vk) => format!("松开 {}", key_name(vk)),
        Action::MouseDown(b) => format!("按下鼠标 {}", button_name(b)),
        Action::MouseUp(b) => format!("松开鼠标 {}", button_name(b)),
    }
}

pub fn button_name(b: MouseButton) -> &'static str {
    match b {
        MouseButton::Left => "左键",
        MouseButton::Middle => "中键",
        MouseButton::Right => "右键",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::score::parse;

    fn actions_of(score: &str) -> Vec<TimedAction> {
        let song = parse(score).unwrap();
        let cfg = Config::default();
        build_actions(&song, &cfg, 0)
    }

    #[test]
    fn note_produces_down_then_up() {
        let acts = actions_of("bpm=120\nticks_per_beat=12\n[score]\nM1/12");
        let downs: Vec<_> = acts
            .iter()
            .filter(|a| matches!(a.action, Action::KeyDown(_)))
            .collect();
        let ups: Vec<_> = acts
            .iter()
            .filter(|a| matches!(a.action, Action::KeyUp(_)))
            .collect();
        assert_eq!(downs.len(), 1);
        assert_eq!(ups.len(), 1);
        assert!(ups[0].at_ms > downs[0].at_ms);
        // 中音区不需要鼠标修饰
        assert!(!acts
            .iter()
            .any(|a| matches!(a.action, Action::MouseDown(_))));
    }

    #[test]
    fn low_register_adds_mouse_modifier_before_note() {
        let acts = actions_of("bpm=120\nticks_per_beat=12\n[score]\nL1/12");
        let mouse_down = acts
            .iter()
            .find(|a| matches!(a.action, Action::MouseDown(_)))
            .expect("低音区应按下鼠标修饰键");
        let key_down = acts
            .iter()
            .find(|a| matches!(a.action, Action::KeyDown(_)))
            .unwrap();
        assert!(mouse_down.at_ms < key_down.at_ms);
    }

    #[test]
    fn repeated_identical_notes_are_separated() {
        let acts = actions_of("bpm=120\nticks_per_beat=12\n[score]\nM1/12 M1/12");
        let key_events: Vec<_> = acts
            .iter()
            .filter(|a| matches!(a.action, Action::KeyDown(_) | Action::KeyUp(_)))
            .collect();
        // 同音重复必须 松-按-松-按
        assert_eq!(key_events.len(), 4);
        assert!(matches!(key_events[1].action, Action::KeyUp(_)));
        assert!(key_events[1].at_ms < key_events[2].at_ms);
    }

    #[test]
    fn shared_modifier_stays_held_across_segments() {
        // L1 接 L1#：左键两段都要用，应该从头按到尾，只在中间补按中键
        let acts = actions_of("bpm=120\nticks_per_beat=12\n[score]\nL1/12 L1#/12");
        let left: Vec<_> = acts
            .iter()
            .filter(|a| {
                matches!(
                    a.action,
                    Action::MouseDown(MouseButton::Left) | Action::MouseUp(MouseButton::Left)
                )
            })
            .collect();
        assert_eq!(
            left.len(),
            2,
            "左键应当只按下一次、松开一次，实际动作：{left:?}"
        );
        assert!(matches!(left[0].action, Action::MouseDown(_)));
        assert!(matches!(left[1].action, Action::MouseUp(_)));

        let middle: Vec<_> = acts
            .iter()
            .filter(|a| {
                matches!(
                    a.action,
                    Action::MouseDown(MouseButton::Middle) | Action::MouseUp(MouseButton::Middle)
                )
            })
            .collect();
        assert_eq!(middle.len(), 2, "中键应当在第二段才按下");
    }

    #[test]
    fn octave_modifier_is_held_for_whole_group() {
        // 连续 7 个低音：左键要按住整组，而不是每个音点一下
        let acts = actions_of("bpm=120\nticks_per_beat=12\n[score]\nL1/12 L2/12 L3/12 L4/12 L5/12 L6/12 L7/12");
        let down = acts
            .iter()
            .find(|a| matches!(a.action, Action::MouseDown(MouseButton::Left)))
            .unwrap();
        let up = acts
            .iter()
            .find(|a| matches!(a.action, Action::MouseUp(MouseButton::Left)))
            .unwrap();
        // 7 个音，每音 500ms，总共约 3.5 秒
        assert!(
            up.at_ms - down.at_ms > 3000.0,
            "左键只按住了 {} ms，应当是整组长按",
            up.at_ms - down.at_ms
        );
    }

    #[test]
    fn modifier_waits_until_previous_note_ends() {
        // 低音短音紧接高音：左键要松开、右键要按下，
        // 两个动作都不能早于低音的音符键松开 —— 否则音符还在响时音高就变了。
        let song = parse("bpm=120\nticks_per_beat=24\n[score]\nL1/3 H1/24").unwrap();
        let cfg = Config::default();
        let acts = build_actions(&song, &cfg, 0);
        let safety = cfg.timing.note_gap_ms as f64;

        let note_up = acts
            .iter()
            .find(|a| matches!(a.action, Action::KeyUp(_)))
            .expect("低音应当有松开");
        let left_up = acts
            .iter()
            .find(|a| matches!(a.action, Action::MouseUp(MouseButton::Left)))
            .expect("左键应当被松开");
        let right_down = acts
            .iter()
            .find(|a| matches!(a.action, Action::MouseDown(MouseButton::Right)))
            .expect("右键应当被按下");

        assert!(
            left_up.at_ms >= note_up.at_ms + safety,
            "左键在 {:.0}ms 就松开了，而音符键 {:.0}ms 才松开，会发出难听的滑音",
            left_up.at_ms,
            note_up.at_ms
        );
        assert!(
            right_down.at_ms >= note_up.at_ms + safety,
            "右键在 {:.0}ms 就按下了，而音符键 {:.0}ms 才松开",
            right_down.at_ms,
            note_up.at_ms
        );
    }

    #[test]
    fn modifier_release_never_beats_note_release() {
        // 整组低音结束时，修饰键的松开也必须晚于最后一个音的松开
        let song =
            parse("bpm=120\nticks_per_beat=24\n[score]\nL1/3 L2/3 L3/24").unwrap();
        let cfg = Config::default();
        let acts = build_actions(&song, &cfg, 0);

        let last_note_up = acts
            .iter()
            .filter(|a| matches!(a.action, Action::KeyUp(_)))
            .map(|a| a.at_ms)
            .fold(f64::MIN, f64::max);
        let left_up = acts
            .iter()
            .find(|a| matches!(a.action, Action::MouseUp(MouseButton::Left)))
            .expect("左键应当被松开");

        assert!(
            left_up.at_ms >= last_note_up,
            "左键在 {:.0}ms 松开，最后一个音 {:.0}ms 才松",
            left_up.at_ms,
            last_note_up
        );
    }

    #[test]
    fn rest_produces_no_note_actions() {
        let acts = actions_of("bpm=120\nticks_per_beat=12\n[score]\nR/12");
        assert!(!acts
            .iter()
            .any(|a| matches!(a.action, Action::KeyDown(_) | Action::MouseDown(_))));
    }
}
