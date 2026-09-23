; 口琴演奏器 —— Inno Setup 安装包脚本（Inno Setup 7）
;
; 版本号不在这里写死：由命令行 /DAppVersion=x.y.z 传进来，来源是 CMakeLists.txt 的
; project(... VERSION ...)。两处各写一遍迟早会对不上。
;
; 打包前必须先有一份干净的暂存目录（见 README「打包」）：
;   1. Release 配置构建（提权清单由 mt.exe 嵌在 exe 里，跟着 exe 走）
;   2. 把 harmonica-player.exe 拷进空目录，对**那个副本**跑 windeployqt
;
;    ⚠ 一定要带 --qmldir app\qml：我们的 QML 是编进 exe 资源的，
;    windeployqt 扫二进制时认不全 QtQuick.Controls 的样式模块，
;    少了 Qt6QuickControls2Fusion / qml\QtQuick\Controls\Fusion 这些东西，
;    装完的程序会起一个白屏窗口 —— 实测漏过一次。
#ifndef AppVersion
  #error 必须传 /DAppVersion=<版本>，取 CMakeLists.txt 里 project(... VERSION) 那个值
#endif
#ifndef StagingDir
  #define StagingDir "..\build-install\staging"
#endif

#define AppName "口琴演奏器"
#define ExeName "harmonica-player.exe"

[Setup]
; AppId 是升级/卸载的匹配键，一旦发布就不要再改
AppId={{7E1F4C2D-9B4A-4C67-8C1E-2A5D3B7F90C1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=tqk114514
DefaultDirName={autopf}\harmonica-player
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#ExeName}
UninstallDisplayName={#AppName}
OutputDir=..\build-install\output
OutputBaseFilename=harmonica-player-setup-{#AppVersion}
SetupIconFile=..\app\icons\icon.ico
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; 只出 64 位：Qt 套件和 SendInput 那套都是 x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 程序本身启动就要管理员（不给就静静地把按键丢掉），装到 Program Files 也要，
; 所以这里不装成「仅当前用户」
PrivilegesRequired=admin
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "zh"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; 暂存目录整个搬进 {app}，VC 运行库单独放：它是要「执行」的，不是程序文件
;
; 排除项都是实测砍出来的：把暂存目录照这份清单删一遍（152M → 59M），跑起来看
; 界面渲染、Fusion 控件、社区曲库下载、试听发声全都正常，才写进这里。
; 每一项的理由：
;   opengl32sw.dll            软件 OpenGL 回退，玩家机器有硬件 D3D11，用不上
;   dxcompiler.dll / dxil.dll D3D12 的着色器编译器，默认后端是 D3D11，走不到
;   d3dcompiler_47.dll        Win10/11 的 System32 里自带同名文件
;   av*-61.dll 等 ffmpeg 一套  只有 QMediaPlayer 用；试听走 QAudioSink，
;                             用的是 multimedia\windowsmediaplugin 那套原生后端
;   五套样式                   程序里写死了 QQuickStyle::setStyle("Fusion")
;   qmltooling\               QML 调试器，只在开发时有用
Source: "{#StagingDir}\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe,opengl32sw.dll,dxcompiler.dll,dxil.dll,d3dcompiler_47.dll,avcodec-61.dll,avformat-61.dll,avutil-59.dll,swresample-5.dll,swscale-8.dll,multimedia\ffmpegmediaplugin.dll,qmltooling,Qt6QuickControls2Imagine.dll,Qt6QuickControls2ImagineStyleImpl.dll,Qt6QuickControls2Material.dll,Qt6QuickControls2MaterialStyleImpl.dll,Qt6QuickControls2Universal.dll,Qt6QuickControls2UniversalStyleImpl.dll,Qt6QuickControls2FluentWinUI3StyleImpl.dll,Qt6QuickControls2WindowsStyleImpl.dll,qml\QtQuick\Controls\Imagine,qml\QtQuick\Controls\Material,qml\QtQuick\Controls\Universal,qml\QtQuick\Controls\FluentWinUI3,qml\QtQuick\Controls\Windows"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StagingDir}\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

; 示例谱：只在不存在时写进去（升级不覆盖用户自己改的），卸载也不碰
Source: "..\songs\dhs\*.dhs"; DestDir: "{app}\songs\dhs"; \
  Flags: onlyifdoesntexist uninsneveruninstall
Source: "..\songs\numbered_musical_notation\*.score.json"; DestDir: "{app}\songs\numbered_musical_notation"; \
  Flags: onlyifdoesntexist uninsneveruninstall

[Dirs]
; 曲谱和配置都写在程序自己旁边。装到 Program Files 时即便是管理员也别去碰
; 目录 ACL，这里只保证安装器知道这些目录归用户所有
Name: "{app}\songs\dhs"; Permissions: users-modify
Name: "{app}\songs\numbered_musical_notation"; Permissions: users-modify

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#ExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon

[Run]
; Qt 的 MSVC 构建要 VC 运行库。机器上已经有就不折腾
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
  StatusMsg: "正在安装 Visual C++ 运行库…"; Check: not VcRedistInstalled; \
  Flags: skipifdoesntexist
Filename: "{app}\{#ExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
  Flags: nowait postinstall skipifsilent

[Code]
function VcRedistInstalled: Boolean;
var
  Version: String;
begin
  Result := RegQueryStringValue(HKLM,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64', 'Version', Version);
end;

[UninstallDelete]
; 故意留空：运行时长出来的 harmonica.ini 和 songs\ 里的谱子是用户的东西，
; 卸载不该把它们一起删掉（Inno 只会删它装进去的文件 + 空目录）
