; Atmos Bridge Host - Windows installer (Inno Setup 6 or 7).
; Built by CI:  ISCC /DAppVersion=<x.y.z> installer\AtmosBridgeHost.iss
; Inputs (relative to this file): ..\dist-host\ (the app), ..\dist\ (the VST3 bundle), ..\resources\AppIcon.ico

#define AppName "Atmos Bridge Host"
#define AppExe  "Atmos Bridge Host.exe"
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#if FileExists(AddBackslash(CompilerPath) + "Languages\Korean.isl")
  #define HasKorean
#endif

[Setup]
AppId={{42023C7F-3C1D-4B27-823A-7822775012E4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Atmos Bridge
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
SetupIconFile=..\resources\AppIcon.ico
OutputDir=..\out
OutputBaseFilename=Atmos-Bridge-Host-Install-win64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
UsedUserAreasWarning=no
CloseApplications=no
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
ShowLanguageDialog=auto

[Languages]
#ifdef HasKorean
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
#endif
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
#ifdef HasKorean
korean.Vst3Task=VST3 플러그인도 설치 (Atmos Bridge 7.1.4ch → Common Files\VST3)
korean.Vst3Group=플러그인:
#endif
english.Vst3Task=Also install the VST3 plugin (Atmos Bridge 7.1.4ch -> Common Files\VST3)
english.Vst3Group=Plugin:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "vst3"; Description: "{cm:Vst3Task}"; GroupDescription: "{cm:Vst3Group}"; Flags: unchecked

[Files]
Source: "..\dist-host\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist-host\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\Atmos Bridge 7.1.4ch.vst3\*"; DestDir: "{commoncf64}\VST3\Atmos Bridge 7.1.4ch.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs; Tasks: vst3

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; The app writes this value itself when "start with Windows" is on; remove it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "{#AppName}"; \
    ValueType: none; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
// Ends a running copy (installed or portable): "--quit" is handed to the running copy, which saves its
// settings and exits; anything still running after that is ended by force.
procedure StopRunningHost(const Exe: String);
var
  Rc: Integer;
begin
  if FileExists(Exe) then
  begin
    Exec(Exe, '--quit', '', SW_HIDE, ewWaitUntilTerminated, Rc);
    Sleep(2000);
  end;
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM "{#AppExe}"', '', SW_HIDE, ewWaitUntilTerminated, Rc);
  Sleep(500);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  ExtractTemporaryFile('{#AppExe}');
  StopRunningHost(ExpandConstant('{tmp}\{#AppExe}'));
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  StopRunningHost(ExpandConstant('{app}\{#AppExe}'));
  Result := True;
end;
