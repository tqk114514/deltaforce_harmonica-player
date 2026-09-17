//! Windows 输入层：SendInput 封装。
//!
//! 只用系统输入 API，不读游戏内存、不注入进程。

use std::mem::size_of;
use std::sync::atomic::{AtomicU64, Ordering};

use windows::Win32::UI::Input::KeyboardAndMouse::{
    INPUT, INPUT_0, INPUT_KEYBOARD, INPUT_MOUSE, KEYBDINPUT, KEYEVENTF_KEYUP,
    KEYEVENTF_SCANCODE, MAPVK_VK_TO_VSC, MOUSEINPUT, MOUSE_EVENT_FLAGS, MOUSEEVENTF_LEFTDOWN,
    MOUSEEVENTF_LEFTUP, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, MOUSEEVENTF_RIGHTDOWN,
    MOUSEEVENTF_RIGHTUP, MapVirtualKeyW, SendInput, VIRTUAL_KEY,
};

use crate::config::MouseButton;

/// 发送统计。SendInput 被系统拦截时是静默失败的，必须自己数。
pub static SENT_OK: AtomicU64 = AtomicU64::new(0);
pub static SENT_FAILED: AtomicU64 = AtomicU64::new(0);

pub fn stats() -> (u64, u64) {
    (
        SENT_OK.load(Ordering::Relaxed),
        SENT_FAILED.load(Ordering::Relaxed),
    )
}

pub fn reset_stats() {
    SENT_OK.store(0, Ordering::Relaxed);
    SENT_FAILED.store(0, Ordering::Relaxed);
}

pub fn key_down(vk: u16) -> bool {
    send_key(vk, false)
}

pub fn key_up(vk: u16) -> bool {
    send_key(vk, true)
}

/// 用扫描码模式发送键盘事件。
/// 部分游戏只认硬件扫描码，不认虚拟键码，所以 wVk 留空、只给 wScan。
fn send_key(vk: u16, up: bool) -> bool {
    let scan = unsafe { MapVirtualKeyW(vk as u32, MAPVK_VK_TO_VSC) } as u16;
    let mut flags = KEYEVENTF_SCANCODE;
    if up {
        flags |= KEYEVENTF_KEYUP;
    }
    let input = INPUT {
        r#type: INPUT_KEYBOARD,
        Anonymous: INPUT_0 {
            ki: KEYBDINPUT {
                wVk: VIRTUAL_KEY(0),
                wScan: scan,
                dwFlags: flags,
                time: 0,
                dwExtraInfo: 0,
            },
        },
    };
    push(&[input])
}

/// 鼠标键动作。用枚举而不是 bool —— 之前传反过一次，
/// 结果把"按下"发成了"抬起"，导致鼠标键卡住、整个系统点不动。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MouseAction {
    Down,
    Up,
}

pub fn mouse_down(button: MouseButton) -> bool {
    mouse(button, MouseAction::Down)
}

pub fn mouse_up(button: MouseButton) -> bool {
    mouse(button, MouseAction::Up)
}

/// 鼠标键 -> SendInput 标志。抽成纯函数是为了能被测试盯住。
pub fn mouse_flags(button: MouseButton, action: MouseAction) -> MOUSE_EVENT_FLAGS {
    match (button, action) {
        (MouseButton::Left, MouseAction::Down) => MOUSEEVENTF_LEFTDOWN,
        (MouseButton::Left, MouseAction::Up) => MOUSEEVENTF_LEFTUP,
        (MouseButton::Middle, MouseAction::Down) => MOUSEEVENTF_MIDDLEDOWN,
        (MouseButton::Middle, MouseAction::Up) => MOUSEEVENTF_MIDDLEUP,
        (MouseButton::Right, MouseAction::Down) => MOUSEEVENTF_RIGHTDOWN,
        (MouseButton::Right, MouseAction::Up) => MOUSEEVENTF_RIGHTUP,
    }
}

pub fn mouse(button: MouseButton, action: MouseAction) -> bool {
    let flags = mouse_flags(button, action);
    let input = INPUT {
        r#type: INPUT_MOUSE,
        Anonymous: INPUT_0 {
            mi: MOUSEINPUT {
                dx: 0,
                dy: 0,
                mouseData: 0,
                dwFlags: flags,
                time: 0,
                dwExtraInfo: 0,
            },
        },
    };
    push(&[input])
}

fn push(inputs: &[INPUT]) -> bool {
    let sent = unsafe { SendInput(inputs, size_of::<INPUT>() as i32) };
    if sent == inputs.len() as u32 {
        SENT_OK.fetch_add(sent as u64, Ordering::Relaxed);
        true
    } else {
        // 常见原因：目标进程以管理员运行而本程序不是，Windows 会静默丢弃
        SENT_FAILED.fetch_add(inputs.len() as u64, Ordering::Relaxed);
        false
    }
}

/// 释放所有可能仍处于按下状态的键与鼠标键。停止、退出的每条路径都要调。
pub fn release_all(keys: &[u16]) {
    for vk in keys {
        key_up(*vk);
    }
    for b in [MouseButton::Left, MouseButton::Middle, MouseButton::Right] {
        mouse_up(b);
    }
}

/// Ctrl+C 兜底：不依赖配置，直接释放默认键位集合。
pub fn emergency_release() {
    const DEFAULT_KEYS: [u16; 8] = [
        b'Z' as u16,
        b'X' as u16,
        b'C' as u16,
        b'V' as u16,
        b'B' as u16,
        b'N' as u16,
        b'M' as u16,
        0xBC,
    ];
    release_all(&DEFAULT_KEYS);
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 曾经把 Down/Up 传反，导致"按下"发成"抬起"、鼠标键卡死。
    /// 这个测试盯着映射方向。
    #[test]
    fn mouse_flags_are_not_swapped() {
        use MouseAction::{Down, Up};
        use MouseButton::{Left, Middle, Right};

        assert_eq!(mouse_flags(Left, Down), MOUSEEVENTF_LEFTDOWN);
        assert_eq!(mouse_flags(Left, Up), MOUSEEVENTF_LEFTUP);
        assert_eq!(mouse_flags(Right, Down), MOUSEEVENTF_RIGHTDOWN);
        assert_eq!(mouse_flags(Right, Up), MOUSEEVENTF_RIGHTUP);
        assert_eq!(mouse_flags(Middle, Down), MOUSEEVENTF_MIDDLEDOWN);
        assert_eq!(mouse_flags(Middle, Up), MOUSEEVENTF_MIDDLEUP);

        // DOWN 和 UP 必须是不同的标志，且 DOWN 不该等于任何 UP
        assert_ne!(mouse_flags(Left, Down), mouse_flags(Left, Up));
        assert_ne!(mouse_flags(Left, Down), mouse_flags(Right, Up));
    }
}
