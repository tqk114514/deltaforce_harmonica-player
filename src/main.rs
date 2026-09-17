//! 三角洲行动 · 口琴自动演奏器
//!
//! 读取文本简谱，按时间轴向游戏窗口发送按键。不读内存、不注入进程。
//!
//! 无参数启动 -> 交互式菜单（双击 exe 的用法）
//! 带参数启动 -> 命令行模式（供 .bat 拖拽使用）

mod config;
mod input;
mod player;
mod score;

use std::io::{self, BufRead, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, MutexGuard};
use std::thread::{self, JoinHandle};
use std::time::{Duration, Instant};

use windows::Win32::System::Console::SetConsoleCtrlHandler;
use windows::Win32::UI::Input::KeyboardAndMouse::{
    HOT_KEY_MODIFIERS, RegisterHotKey, UnregisterHotKey,
};
use windows::Win32::UI::WindowsAndMessaging::{GetMessageW, MSG, WM_HOTKEY};
use windows::core::BOOL;

use config::Config;
use player::{Control, PlayState, TimedAction, play};
use score::{Event, Pitch, Register, Song};

const HOTKEY_START: i32 = 1;
const HOTKEY_STOP: i32 = 2;

const VK_F9: u32 = 0x78;
const VK_F10: u32 = 0x79;
const MOD_NOREPEAT: u32 = 0x4000;

// ---------------------------------------------------------------- 数据结构

struct SongEntry {
    #[allow(dead_code)]
    path: PathBuf,
    song: Song,
}

struct Playback {
    ctl: Control,
    handle: Option<JoinHandle<()>>,
    done: Arc<AtomicBool>,
    /// 倒计时结束、真正开始演奏后为 true
    started: Arc<AtomicBool>,
    progress: Arc<AtomicU64>,
    total_ms: f64,
    #[allow(dead_code)]
    title: String,
}

struct App {
    cfg: Mutex<Config>,
    base: PathBuf,
    songs: Mutex<Vec<SongEntry>>,
    /// 当前选中的曲目，F9 重播用
    selected: Mutex<Option<usize>>,
    /// 预构建的动作表
    plan: Mutex<Option<Arc<Vec<TimedAction>>>>,
    playing: Mutex<Option<Playback>>,
    lead: AtomicU64,
    shift: AtomicI32,
    /// 演奏速度百分比，100 = 原速。游戏来不及响应时往下调。
    speed: AtomicU32,
}

impl App {
    fn new(base: PathBuf, cfg: Config) -> App {
        App {
            cfg: Mutex::new(cfg),
            base,
            songs: Mutex::new(Vec::new()),
            selected: Mutex::new(None),
            plan: Mutex::new(None),
            playing: Mutex::new(None),
            lead: AtomicU64::new(3),
            shift: AtomicI32::new(0),
            speed: AtomicU32::new(100),
        }
    }

    fn scan_songs(&self) {
        let dir = self.base.join("songs");
        let mut entries = Vec::new();
        if let Ok(rd) = std::fs::read_dir(&dir) {
            let mut paths: Vec<PathBuf> = rd
                .filter_map(|e| e.ok())
                .map(|e| e.path())
                .filter(|p| p.extension().is_some_and(|x| x.eq_ignore_ascii_case("dhs")))
                .collect();
            paths.sort();
            for p in paths {
                match load_song(&p) {
                    Ok(song) => entries.push(SongEntry { path: p, song }),
                    Err(e) => println!("  跳过 {}: {e}", p.display()),
                }
            }
        }
        *self.songs.lock().unwrap_or_else(|e| e.into_inner()) = entries;
    }

    fn stop(&self) {
        let mut slot = self.playing.lock().unwrap_or_else(|e| e.into_inner());
        if let Some(mut pb) = slot.take() {
            *lock(&pb.ctl) = PlayState::Stopped;
            if let Some(h) = pb.handle.take() {
                let _ = h.join();
            }
            let cfg = self.cfg.lock().unwrap_or_else(|e| e.into_inner());
            input::release_all(&cfg.all_keys());
        }
    }

    /// 构建动作表并开始演奏。dry_run 时不发送输入，直接在当前线程打印。
    fn start(&self, idx: usize, dry_run: bool) {
        self.stop();

        let title;
        let total_ms;
        let plan = {
            let songs = self.songs.lock().unwrap_or_else(|e| e.into_inner());
            let Some(entry) = songs.get(idx) else {
                println!("  没有编号 {} 的曲目。", idx + 1);
                return;
            };
            // 放慢速度：把 BPM 按百分比缩放，每个音的时值就变长了，
            // 游戏才有足够时间把每个音读进去
            let speed = self.speed.load(Ordering::Relaxed);
            let mut song = entry.song.clone();
            if speed != 100 {
                song.bpm *= speed as f64 / 100.0;
            }
            title = song.title.clone();
            total_ms = song.duration_ms();
            let cfg = self.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
            let shift = self.shift.load(Ordering::Relaxed);

            player::build_actions(&song, &cfg, shift)
        };
        let plan = Arc::new(plan);

        *self.selected.lock().unwrap_or_else(|e| e.into_inner()) = Some(idx);
        *self.plan.lock().unwrap_or_else(|e| e.into_inner()) = Some(plan.clone());

        let cfg = self.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();

        if dry_run {
            println!("\n-- 干跑：{title}，以下动作不会真正发送 --\n");
            let ctl: Control = Arc::new(Mutex::new(PlayState::Running));
            let _ = play(&plan, &cfg, ctl, true, None);
            input::release_all(&cfg.all_keys());
            return;
        }

        println!("\n准备演奏：{title}（{}）", fmt_duration(total_ms));
        input::reset_stats();
        let ctl: Control = Arc::new(Mutex::new(PlayState::Running));
        let done = Arc::new(AtomicBool::new(false));
        let started = Arc::new(AtomicBool::new(false));
        let progress = Arc::new(AtomicU64::new(0));
        let lead = self.lead.load(Ordering::Relaxed);

        let handle = {
            let ctl = ctl.clone();
            let cfg = cfg.clone();
            let plan = plan.clone();
            let done = done.clone();
            let started = started.clone();
            let progress = progress.clone();
            let title = title.clone();
            thread::spawn(move || {
                if countdown(&ctl, lead) {
                    started.store(true, Ordering::Relaxed);
                    if let Err(e) = play(&plan, &cfg, ctl.clone(), false, Some(&progress)) {
                        eprintln!("演奏出错：{e}");
                    }
                    println!("\n演奏结束：{title}");
                }
                done.store(true, Ordering::Relaxed);
            })
        };

        *self
            .playing
            .lock()
            .unwrap_or_else(|e| e.into_inner()) = Some(Playback {
            ctl,
            handle: Some(handle),
            done,
            started,
            progress,
            total_ms,
            title,
        });
    }

    /// 等待演奏结束，期间显示进度
    fn wait_finish(&self) {
        loop {
            let finished = {
                let slot = self.playing.lock().unwrap_or_else(|e| e.into_inner());
                match slot.as_ref() {
                    None => true,
                    Some(pb) => {
                        // 倒计时阶段由演奏线程打印，这里保持安静，避免两处同时写屏
                        if pb.started.load(Ordering::Relaxed) {
                            let ms = pb.progress.load(Ordering::Relaxed) as f64;
                            print!(
                                "\r  演奏中 {} / {}   （F10 停止）   ",
                                fmt_duration(ms),
                                fmt_duration(pb.total_ms)
                            );
                            io::stdout().flush().ok();
                        }
                        pb.done.load(Ordering::Relaxed)
                    }
                }
            };
            if finished {
                break;
            }
            thread::sleep(Duration::from_millis(200));
        }
        println!();
        self.stop();
        report_input_stats();
    }
}

fn lock(ctl: &Control) -> MutexGuard<'_, PlayState> {
    ctl.lock().unwrap_or_else(|e| e.into_inner())
}

/// 报告 SendInput 的发送结果。被系统丢弃时给出可操作的原因。
fn report_input_stats() {
    let (ok, failed) = input::stats();
    if failed > 0 {
        println!("  输入发送：{ok} 个成功，{failed} 个被系统丢弃。");
        println!("  被丢弃多半是权限问题：请确认本程序和游戏都以管理员身份运行。");
    } else if ok > 0 {
        println!("  输入发送：{ok} 个，全部成功。");
    }
}

// ---------------------------------------------------------------- 入口

fn main() {
    unsafe {
        let _ = windows::Win32::System::Console::SetConsoleOutputCP(65001);
    }
    unsafe {
        let _ = SetConsoleCtrlHandler(Some(ctrl_handler), true);
    }

    ensure_admin();

    let args: Vec<String> = std::env::args().skip(1).collect();
    let base = find_base_dir();
    let cfg_path = base.join("harmonica.ini");
    let cfg = match Config::load(&cfg_path) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("配置错误：{e}\n将使用内置默认键位。");
            Config::default()
        }
    };

    if args.is_empty() {
        interactive(Arc::new(App::new(base, cfg)));
    } else {
        cli_mode(base, cfg, args);
    }
}

/// 游戏通常以管理员权限运行。Windows 的 UIPI 会静默丢弃低权限进程发往高权限进程的输入，
/// 所以本程序也必须是管理员，否则键盘鼠标事件一个都进不了游戏。
fn ensure_admin() {
    if is_elevated() {
        return;
    }
    println!("检测到当前不是管理员权限。游戏若是管理员运行，Windows 会拦截模拟输入。");
    println!("正在请求管理员权限重新启动...");
    if relaunch_as_admin() {
        std::process::exit(0);
    }
    eprintln!("提权被取消或失败。请右键 exe →「以管理员身份运行」，否则游戏收不到模拟按键。\n");
}

fn is_elevated() -> bool {
    use windows::Win32::Foundation::{CloseHandle, HANDLE};
    use windows::Win32::Security::{
        GetTokenInformation, TOKEN_ELEVATION, TOKEN_QUERY, TokenElevation,
    };
    use windows::Win32::System::Threading::{GetCurrentProcess, OpenProcessToken};

    unsafe {
        let mut token = HANDLE::default();
        if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token).is_err() {
            return false;
        }
        let mut elevation = TOKEN_ELEVATION::default();
        let mut size = 0u32;
        let ok = GetTokenInformation(
            token,
            TokenElevation,
            Some(&mut elevation as *mut _ as *mut core::ffi::c_void),
            std::mem::size_of::<TOKEN_ELEVATION>() as u32,
            &mut size,
        )
        .is_ok();
        let _ = CloseHandle(token);
        ok && elevation.TokenIsElevated != 0
    }
}

fn relaunch_as_admin() -> bool {
    use windows::Win32::UI::Shell::ShellExecuteW;
    use windows::Win32::UI::WindowsAndMessaging::SW_SHOWNORMAL;
    use windows::core::PCWSTR;

    let Ok(exe) = std::env::current_exe() else {
        return false;
    };
    let params = std::env::args()
        .skip(1)
        .map(|a| {
            if a.contains(' ') {
                format!("\"{a}\"")
            } else {
                a
            }
        })
        .collect::<Vec<_>>()
        .join(" ");

    let op = to_wide("runas");
    let file = to_wide(&exe.to_string_lossy());
    let param_w = to_wide(&params);

    let result = unsafe {
        ShellExecuteW(
            None,
            PCWSTR(op.as_ptr()),
            PCWSTR(file.as_ptr()),
            PCWSTR(param_w.as_ptr()),
            None,
            SW_SHOWNORMAL,
        )
    };
    // ShellExecuteW 返回值 > 32 表示成功
    result.0 as isize > 32
}

fn to_wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// 双击 exe 时工作目录是 exe 所在目录，需要自己往上找到 songs/ 所在的基准目录
fn find_base_dir() -> PathBuf {
    if let Ok(exe) = std::env::current_exe() {
        if let Some(mut dir) = exe.parent().map(|p| p.to_path_buf()) {
            for _ in 0..5 {
                if dir.join("songs").is_dir() || dir.join("harmonica.ini").is_file() {
                    return dir;
                }
                if !dir.pop() {
                    break;
                }
            }
        }
    }
    std::env::current_dir().unwrap_or_else(|_| PathBuf::from("."))
}

// ---------------------------------------------------------------- 交互菜单

fn interactive(app: Arc<App>) {
    app.scan_songs();

    let (_hk, hotkey_status) = spawn_hotkeys(app.clone());

    println!("\n=== 三角洲行动 · 口琴自动演奏器 ===");
    println!("键位来自社区资料，首次使用请先跑一次 c 键位校准。");

    loop {
        println!();
        println!("--------------------------------------------------");
        {
            let cfg = app.cfg.lock().unwrap_or_else(|e| e.into_inner());
            println!(
                "倒计时 {} 秒 | 速度 {}% | 音区平移 {:+} | 按键 {}..{}",
                app.lead.load(Ordering::Relaxed),
                app.speed.load(Ordering::Relaxed),
                app.shift.load(Ordering::Relaxed),
                config::key_name(cfg.degree_keys[0]),
                config::key_name(cfg.degree_keys[6]),
            );
            println!(
                "时序：修饰键提前 {} ms / 松键间隔 {} ms",
                cfg.timing.mouse_lead_ms, cfg.timing.note_gap_ms
            );
            println!("{}", format_hotkeys(&hotkey_status));
        }

        {
            let songs = app.songs.lock().unwrap_or_else(|e| e.into_inner());
            if songs.is_empty() {
                println!("\n没有找到曲谱。把 .dhs 文件放进 songs 目录后按 r 重新扫描。");
            } else {
                println!("\n曲目：");
                for (i, e) in songs.iter().enumerate() {
                    let name = if e.song.artist.is_empty() {
                        e.song.title.clone()
                    } else {
                        format!("{} / {}", e.song.title, e.song.artist)
                    };
                    println!(
                        "  [{:>2}] {:<22} {:>3.0} BPM  {:>5}  {}个音",
                        i + 1,
                        name,
                        e.song.bpm,
                        fmt_duration(e.song.duration_ms()),
                        e.song.note_count(),
                    );
                }
            }
        }

        println!();
        println!("  编号 = 演奏      d+编号 = 干跑（只看按键不发送）");
        println!("  c 键位校准       s 设置        r 重新扫描      q 退出");
        print!("> ");
        io::stdout().flush().ok();

        let mut line = String::new();
        if io::stdin().lock().read_line(&mut line).is_err() {
            break;
        }
        let cmd = line.trim().to_ascii_lowercase();
        if cmd.is_empty() {
            continue;
        }

        match cmd.as_str() {
            "q" | "quit" | "exit" => {
                app.stop();
                break;
            }
            "r" => {
                app.scan_songs();
                println!("  已重新扫描。");
            }
            "c" => {
                app.stop();
                run_calibration(&app, false);
            }
            "s" => settings_menu(&app),
            other => {
                let (dry, rest) = match other.strip_prefix('d') {
                    Some(r) => (true, r.trim()),
                    None => (false, other),
                };
                match rest.parse::<usize>() {
                    Ok(n) if n >= 1 => {
                        app.start(n - 1, dry);
                        if !dry {
                            app.wait_finish();
                        }
                    }
                    _ => println!("  看不懂「{other}」。输入编号，或 q 退出。"),
                }
            }
        }
    }

    app.stop();
    // 热键线程还在等消息，直接结束进程
    std::process::exit(0);
}

fn settings_menu(app: &Arc<App>) {
    println!("\n设置（直接回车表示保持不变）");
    println!("  游戏里听不到某些音时，先把「演奏速度」调低试试，比如 80 或 70。");

    if let Some(v) = prompt_i64(
        &format!("倒计时秒数 [当前 {}]：", app.lead.load(Ordering::Relaxed)),
        0,
        60,
    ) {
        app.lead.store(v as u64, Ordering::Relaxed);
    }

    if let Some(v) = prompt_i64(
        &format!(
            "音区平移 -1 低八度 / 0 不变 / 1 高八度 [当前 {:+}]：",
            app.shift.load(Ordering::Relaxed)
        ),
        -1,
        1,
    ) {
        app.shift.store(v as i32, Ordering::Relaxed);
    }

    if let Some(v) = prompt_i64(
        &format!(
            "演奏速度 %（100=原速，游戏吞音就往下调，比如 80）[当前 {}]：",
            app.speed.load(Ordering::Relaxed)
        ),
        40,
        200,
    ) {
        app.speed.store(v as u32, Ordering::Relaxed);
    }

    let t = app.cfg.lock().unwrap_or_else(|e| e.into_inner()).timing;

    if let Some(v) = prompt_i64(
        &format!(
            "修饰键提前量 ms（降调/升调/半音提前多久按下；游戏里修饰没生效就调大）[当前 {}]：",
            t.mouse_lead_ms
        ),
        0,
        500,
    ) {
        app.cfg
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .timing
            .mouse_lead_ms = v as u64;
    }

    if let Some(v) = prompt_i64(
        &format!("松键间隔 ms（松开一个音后隔多久按下一个音）[当前 {}]：", t.note_gap_ms),
        0,
        500,
    ) {
        app.cfg
            .lock()
            .unwrap_or_else(|e| e.into_inner())
            .timing
            .note_gap_ms = v as u64;
    }

    println!("  设置已更新（本次运行有效，要长期生效请写进 harmonica.ini）。");
}

fn prompt_i64(prompt: &str, min: i64, max: i64) -> Option<i64> {
    print!("{prompt}");
    io::stdout().flush().ok();
    let mut line = String::new();
    if io::stdin().lock().read_line(&mut line).is_err() {
        return None;
    }
    let t = line.trim();
    if t.is_empty() {
        return None;
    }
    match t.parse::<i64>() {
        Ok(v) if (min..=max).contains(&v) => Some(v),
        Ok(_) => {
            println!("  超出范围（{min}..{max}），保持不变。");
            None
        }
        Err(_) => {
            println!("  不是数字，保持不变。");
            None
        }
    }
}

// ---------------------------------------------------------------- 热键

/// 热键定义：(id, 虚拟键码, 显示名, 用途说明)
const HOTKEYS: [(i32, u32, &str, &str); 2] = [
    (HOTKEY_START, VK_F9, "F9", "重播"),
    (HOTKEY_STOP, VK_F10, "F10", "停止"),
];

/// 启动热键线程，并同步等待注册结果，避免后台打印打乱菜单。
/// 返回 (线程句柄, 每个热键是否注册成功)
fn spawn_hotkeys(app: Arc<App>) -> (JoinHandle<()>, Vec<bool>) {
    let (tx, rx) = std::sync::mpsc::channel();
    let handle = thread::spawn(move || hotkey_loop(app, tx));
    let status = rx.recv().unwrap_or_else(|_| vec![false; HOTKEYS.len()]);
    (handle, status)
}

fn format_hotkeys(status: &[bool]) -> String {
    let ok: Vec<String> = HOTKEYS
        .iter()
        .zip(status.iter())
        .filter(|(_, registered)| **registered)
        .map(|((_, _, name, desc), _)| format!("{name} {desc}"))
        .collect();
    format!("热键：{}", ok.join(" / "))
}

fn hotkey_loop(app: Arc<App>, ready: std::sync::mpsc::Sender<Vec<bool>>) {
    let mut status = Vec::new();
    unsafe {
        for (id, vk, _name, _desc) in HOTKEYS {
            status.push(RegisterHotKey(None, id, HOT_KEY_MODIFIERS(MOD_NOREPEAT), vk).is_ok());
        }
    }
    let _ = ready.send(status);

    let mut msg = MSG::default();
    loop {
        let fetched = unsafe { GetMessageW(&mut msg, None, 0, 0) };
        if !fetched.as_bool() {
            break;
        }
        if msg.message != WM_HOTKEY {
            continue;
        }
        match msg.wParam.0 as i32 {
            HOTKEY_START => {
                let idx = *app.selected.lock().unwrap_or_else(|e| e.into_inner());
                match idx {
                    Some(i) => app.start(i, false),
                    None => println!("\n  还没有选过曲目，先在菜单里选一首。"),
                }
            }
            HOTKEY_STOP => {
                app.stop();
                println!("\n  已停止。");
            }
            _ => {}
        }
    }

    unsafe {
        for id in [HOTKEY_START, HOTKEY_STOP] {
            let _ = UnregisterHotKey(None, id);
        }
    }
}

// ---------------------------------------------------------------- 命令行模式

const USAGE: &str = "\
三角洲行动 · 口琴自动演奏器

用法:
  harmonica-player                      交互式菜单（双击 exe 的默认用法）
  harmonica-player <谱文件.dhs> [选项]   命令行模式
  harmonica-player --calibrate          键位校准
  harmonica-player --init-config        生成默认配置文件 harmonica.ini

选项:
  --config <FILE>    指定配置文件
  --dry-run          只打印将要发送的动作，不真正发送
  --lead <秒>        开始前的倒计时秒数，默认 3
  --shift <-1|0|1>   整体音区平移
  --speed <百分比>   演奏速度，默认 100。游戏吞音时调低，比如 80
  --once             演奏结束后自动退出

热键:  F9 开始/重播   F10 停止
";

struct Options {
    score: Option<PathBuf>,
    config: Option<PathBuf>,
    dry_run: bool,
    calibrate: bool,
    init_config: bool,
    lead: u64,
    shift: i32,
    speed: u32,
    once: bool,
}

fn cli_mode(base: PathBuf, cfg: Config, args: Vec<String>) {
    let opts = match parse_args(args) {
        Ok(o) => o,
        Err(e) => {
            if !e.is_empty() {
                eprintln!("{e}\n");
            }
            print!("{USAGE}");
            std::process::exit(2);
        }
    };

    let cfg_path = opts
        .config
        .clone()
        .unwrap_or_else(|| base.join("harmonica.ini"));

    if opts.init_config {
        match std::fs::write(&cfg_path, Config::default_ini_text()) {
            Ok(_) => println!("已生成配置文件 {}", cfg_path.display()),
            Err(e) => {
                eprintln!("写配置失败：{e}");
                std::process::exit(1);
            }
        }
        return;
    }

    let cfg = if cfg_path.exists() {
        match Config::load(&cfg_path) {
            Ok(c) => c,
            Err(e) => {
                eprintln!("配置错误：{e}");
                std::process::exit(1);
            }
        }
    } else {
        cfg
    };

    if opts.calibrate {
        let app = Arc::new(App::new(base, cfg));
        app.lead.store(opts.lead, Ordering::Relaxed);
        run_calibration(&app, opts.dry_run);
        return;
    }

    let Some(score_path) = opts.score.clone() else {
        eprintln!("缺少谱文件路径。\n");
        print!("{USAGE}");
        std::process::exit(2);
    };

    let song = match load_song(&score_path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(1);
        }
    };

    println!(
        "曲目：{}   {} BPM   {}",
        song.title,
        song.bpm,
        fmt_duration(song.duration_ms())
    );

    if opts.dry_run {
        let mut song = song.clone();
        if opts.speed != 100 {
            song.bpm *= opts.speed as f64 / 100.0;
        }
        let actions = Arc::new(player::build_actions(&song, &cfg, opts.shift));
        println!("\n--dry-run：以下动作不会真正发送\n");
        let ctl: Control = Arc::new(Mutex::new(PlayState::Running));
        let _ = play(&actions, &cfg, ctl, true, None);
        return;
    }

    let app = Arc::new(App::new(base, cfg));
    app.lead.store(opts.lead, Ordering::Relaxed);
    app.shift.store(opts.shift, Ordering::Relaxed);
    app.speed.store(opts.speed, Ordering::Relaxed);
    *app.songs.lock().unwrap_or_else(|e| e.into_inner()) =
        vec![SongEntry { path: score_path, song }];

    let (_hk, status) = spawn_hotkeys(app.clone());
    println!("{}", format_hotkeys(&status));

    app.start(0, false);
    app.wait_finish();

    if opts.once {
        std::process::exit(0);
    }

    // 命令行模式下继续等待 F9 重播
    loop {
        thread::sleep(Duration::from_millis(500));
    }
}

// ---------------------------------------------------------------- 公共工具

/// 倒计时，给用户留出切回游戏的时间。被停止则返回 false。
fn countdown(control: &Control, secs: u64) -> bool {
    if secs == 0 {
        return true;
    }
    println!("{secs} 秒后开始演奏，请把焦点切回游戏...");
    let until = Instant::now() + Duration::from_secs(secs);
    let mut last_shown = secs + 1;
    loop {
        if *lock(control) == PlayState::Stopped {
            return false;
        }
        let now = Instant::now();
        if now >= until {
            break;
        }
        let left = (until - now).as_secs_f32().ceil() as u64;
        if left != last_shown {
            println!("  {left}...");
            last_shown = left;
        }
        thread::sleep(Duration::from_millis(50));
    }
    println!("开始！");
    true
}

fn load_song(path: &Path) -> Result<Song, String> {
    let text = std::fs::read_to_string(path)
        .map_err(|e| format!("读取谱文件 {} 失败：{e}", path.display()))?;
    let text = text.trim_start_matches('\u{feff}');
    score::parse(text)
}

/// 校准顺序：(音区, 是否升半音, 该组吹几个音, 显示名)
/// 从不修饰开始，逐步叠加修饰；超高音区只有 do，所以只吹 1 个音。
/// 注意：高音区组内修饰并不一致（H1 直按第 8 键不按右键，H2..H7 才按右键），
/// 所以不能"整组共用一套修饰键"，必须走区间合并。
const CALIBRATION_GROUPS: [(Register, bool, u8, &str); 8] = [
    (Register::Mid, false, 7, "不修饰"),
    (Register::Mid, true, 7, "半音"),
    (Register::Low, false, 7, "降调"),
    (Register::Low, true, 7, "降调 + 半音"),
    (Register::High, false, 7, "升调"),
    (Register::High, true, 7, "升调 + 半音"),
    (Register::Top, false, 1, "升调 + 逗号键"),
    (Register::Top, true, 1, "升调 + 半音 + 逗号键"),
];

/// 键位校准：按 CALIBRATION_GROUPS 的顺序把各组合吹一遍。
///
/// 走的是和正式演奏同一套动作表（build_actions），因此修饰键的区间合并、按下提前量、
/// 松开顺序都和演奏时一致，不会出现"校准听着对、演奏却不对"。
/// 注册进 app.playing，F10 能像控制演奏一样中断它。
fn run_calibration(app: &Arc<App>, dry: bool) {
    let cfg = app.cfg.lock().unwrap_or_else(|e| e.into_inner()).clone();
    let lead = app.lead.load(Ordering::Relaxed);

    // 每个音 14 tick（约 0.6 秒），间隔 5 tick 休止；120 BPM 下每 tick 约 41.7 ms
    let mut events = Vec::new();
    let mut labels: Vec<(&str, u8, Vec<config::MouseButton>, u16)> = Vec::new();
    for (reg, sharp, count, label) in CALIBRATION_GROUPS {
        for degree in 1..=count {
            let (vk, buttons) = cfg.resolve(Pitch {
                register: reg,
                degree,
                sharp,
            });
            labels.push((label, degree, buttons, vk));
            events.push(Event::Note {
                pitch: Pitch {
                    register: reg,
                    degree,
                    sharp,
                },
                ticks: 14,
            });
            events.push(Event::Rest { ticks: 5 });
        }
    }

    println!("键位校准：依次吹响 {} 个音位。", labels.len());
    println!("请对照游戏里听到的音高，确认 harmonica.ini 里的键位与鼠标奏法是否正确。");

    if dry {
        println!("（干跑模式：只列出组合，不会真的发送按键）\n");
        for (label, degree, buttons, vk) in &labels {
            println!(
                "   {label} {degree}   {} + {}",
                modifier_desc(buttons),
                config::key_name(*vk)
            );
        }
        println!("校准结束。");
        return;
    }

    let song = Song {
        title: "校准音阶".into(),
        artist: String::new(),
        bpm: 120.0,
        ticks_per_beat: 12,
        events,
    };
    let actions = player::build_actions(&song, &cfg, 0);

    // 任何退出路径（含 panic）都要松开按键，否则鼠标会卡在按下状态
    let keys = cfg.all_keys();
    let _guard = player::ReleaseGuard::new(&keys, true);

    input::reset_stats();
    let ctl: Control = Arc::new(Mutex::new(PlayState::Running));
    let done = Arc::new(AtomicBool::new(false));
    *app.playing.lock().unwrap_or_else(|e| e.into_inner()) = Some(Playback {
        ctl: ctl.clone(),
        handle: None,
        done: done.clone(),
        started: Arc::new(AtomicBool::new(true)),
        progress: Arc::new(AtomicU64::new(0)),
        total_ms: song.duration_ms(),
        title: "键位校准".into(),
    });

    if !countdown(&ctl, lead) {
        input::release_all(&cfg.all_keys());
        *app.playing.lock().unwrap_or_else(|e| e.into_inner()) = None;
        return;
    }

    let base = Instant::now();
    let mut played = 0usize;

    for ta in &actions {
        loop {
            if *lock(&ctl) == PlayState::Stopped {
                input::release_all(&cfg.all_keys());
                *app.playing.lock().unwrap_or_else(|e| e.into_inner()) = None;
                done.store(true, Ordering::Relaxed);
                println!("\n校准已停止。");
                return;
            }
            let target = base + Duration::from_secs_f64(ta.at_ms / 1000.0);
            let now = Instant::now();
            if now >= target {
                break;
            }
            thread::sleep((target - now).min(Duration::from_millis(50)));
        }

        player::dispatch(ta.action);
        if let player::Action::KeyDown(vk) = ta.action {
            if let Some((label, degree, buttons, _)) = labels.get(played) {
                println!(
                    "   {label} {degree}   {} + {}",
                    modifier_desc(buttons),
                    config::key_name(vk)
                );
            }
            played += 1;
        }
    }

    input::release_all(&cfg.all_keys());
    *app.playing.lock().unwrap_or_else(|e| e.into_inner()) = None;
    done.store(true, Ordering::Relaxed);
    println!("校准结束。");
    report_input_stats();
}

fn modifier_desc(buttons: &[config::MouseButton]) -> String {
    if buttons.is_empty() {
        return "无修饰".to_string();
    }
    buttons
        .iter()
        .map(|b| player::button_name(*b).to_string())
        .collect::<Vec<_>>()
        .join(" + ")
}

fn fmt_duration(ms: f64) -> String {
    let total = (ms / 1000.0).round() as u64;
    format!("{}:{:02}", total / 60, total % 60)
}

fn parse_args(args: Vec<String>) -> Result<Options, String> {
    let mut opts = Options {
        score: None,
        config: None,
        dry_run: false,
        calibrate: false,
        init_config: false,
        lead: 3,
        shift: 0,
        speed: 100,
        once: false,
    };
    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "-h" | "--help" => return Err(String::new()),
            "--dry-run" => opts.dry_run = true,
            "--calibrate" => opts.calibrate = true,
            "--init-config" => opts.init_config = true,
            "--once" => opts.once = true,
            "--config" => {
                let v = iter.next().ok_or("--config 缺少参数")?;
                opts.config = Some(PathBuf::from(v));
            }
            "--lead" => {
                let v = iter.next().ok_or("--lead 缺少参数")?;
                opts.lead = v.parse().map_err(|_| "--lead 需要整数秒数")?;
            }
            "--speed" => {
                let v = iter.next().ok_or("--speed 缺少参数")?;
                opts.speed = v.parse().map_err(|_| "--speed 需要百分比整数")?;
                if !(40..=200).contains(&opts.speed) {
                    return Err("--speed 支持 40..200".to_string());
                }
            }
            "--shift" => {
                let v = iter.next().ok_or("--shift 缺少参数")?;
                opts.shift = v.parse().map_err(|_| "--shift 需要 -1 / 0 / 1")?;
                if !(-1..=1).contains(&opts.shift) {
                    return Err("--shift 只支持 -1 / 0 / 1".to_string());
                }
            }
            other if other.starts_with("--") => return Err(format!("未知选项 {other}")),
            other => opts.score = Some(PathBuf::from(other)),
        }
    }
    Ok(opts)
}

unsafe extern "system" fn ctrl_handler(_ctrl_type: u32) -> BOOL {
    input::emergency_release();
    std::process::exit(0);
}
