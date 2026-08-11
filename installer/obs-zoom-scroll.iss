#define MyAppName "OBS Zoom Scroll"
#define MyAppVersion "0.1.0"
#define MyAppPublisher "OBS Zoom Scroll"
#define MyAppExeName "obs64.exe"

[Setup]
AppId={{8B4C6E6E-6C11-4B70-9BA3-5CC0D7C03D70}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={code:GetOBSDir}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=obs-zoom-scroll-v{#MyAppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName}

[Files]
Source: "..\build\Release\obs-zoom-scroll.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion restartreplace
Source: "..\data\zoom_scroll.effect"; DestDir: "{app}\data\obs-plugins\obs-zoom-scroll"; Flags: ignoreversion
Source: "..\data\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\obs-zoom-scroll\locale"; Flags: ignoreversion

[Code]
function GetOBSDir(Param: String): String;
var
  Candidate: String;
begin
  Candidate := ExpandConstant('{autopf}\obs-studio');
  if FileExists(AddBackslash(Candidate) + 'bin\64bit\obs64.exe') then
  begin
    Result := Candidate;
    exit;
  end;

  if RegQueryStringValue(HKLM64, 'Software\OBS Studio', 'InstallPath', Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  if RegQueryStringValue(HKLM32, 'Software\OBS Studio', 'InstallPath', Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  Result := ExpandConstant('{autopf}\obs-studio');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
    if not FileExists(AddBackslash(WizardDirValue) + 'bin\64bit\obs64.exe') then
      MsgBox('OBS Studioの64bit実行ファイルが見つかりません。インストール先を確認してください。', mbInformation, MB_OK);
end;
