#define AppName "AyuGram Popular Messages 2"
#define AppVersion "6.7.8"
#define AppExe "AyuGram.exe"

[Setup]
AppId={{A37CE13C-77EE-499D-B8B3-659AD14AD53C}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=AyuGram contributors / Popular Messages enhancement
AppPublisherURL=https://github.com/game-pages/AyuGramPopular
DefaultDirName={localappdata}\AyuGramPopular
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputBaseFilename=AyuGramPopular-6.7.8-PopularMessages2-Windows-x64-Setup
SetupIconFile={#SourcePath}\Telegram\Resources\art\icon256.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
AllowNoIcons=yes
CloseApplications=yes
LicenseFile={#SourcePath}\LICENSE
VersionInfoVersion=6.7.8.2

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#ReleasePath}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#ReleasePath}\modules\x64\d3d\d3dcompiler_47.dll"; DestDir: "{app}\modules\x64\d3d"; Flags: ignoreversion
Source: "{#SourcePath}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\LEGAL"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
