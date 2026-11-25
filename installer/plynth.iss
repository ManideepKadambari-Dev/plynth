; Plynth installer for one user. Compile with ISCC and these defines:
;   /DAppVersion=0.1.0 /DStage=<staged folder> /DOutDir=<folder> /DRuntimeMajor=14 /DRuntimeMinor=44
; Optional: /DPathEntry=0 leaves the command line path alone, /DRuntimeKey=<key> points the runtime check
; at another registry key, for the install test.

#ifndef AppVersion
  #error AppVersion is not set
#endif
#ifndef Stage
  #error Stage is not set
#endif
#ifndef OutDir
  #error OutDir is not set
#endif
#ifndef RuntimeMajor
  #error RuntimeMajor is not set
#endif
#ifndef RuntimeMinor
  #error RuntimeMinor is not set
#endif
#ifndef PathEntry
  #define PathEntry "1"
#endif
#ifndef RuntimeKey
  #define RuntimeKey "SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64"
#endif

[Setup]
; The AppId never changes, or Windows sees a second program.
AppId={{8473EE0D-F68A-4C90-BB1C-1D2F3A293B12}
AppName=Plynth
AppVersion={#AppVersion}
AppVerName=Plynth {#AppVersion}
AppPublisher=ManideepKadambari-Dev
AppPublisherURL=https://github.com/ManideepKadambari-Dev/plynth
AppSupportURL=https://github.com/ManideepKadambari-Dev/plynth/issues
AppUpdatesURL=https://github.com/ManideepKadambari-Dev/plynth/releases
DefaultDirName={userpf}\Plynth
DisableDirPage=yes
UsePreviousAppDir=no
DefaultGroupName=Plynth
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutDir}
OutputBaseFilename=PlynthSetup
SetupIconFile={#SourcePath}..\app\resources\plynth.ico
UninstallDisplayIcon={app}\plynth.exe
UninstallDisplayName=Plynth
LicenseFile={#Stage}\LICENSE
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
SetupLogging=yes
#if PathEntry == "1"
ChangesEnvironment=yes
#endif
VersionInfoVersion={#AppVersion}
VersionInfoProductName=Plynth
VersionInfoProductVersion={#AppVersion}
VersionInfoCompany=ManideepKadambari-Dev
VersionInfoDescription=Plynth setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[InstallDelete]
; The program's own script and QML folders are replaced whole, so a file removed in a new version goes.
Type: filesandordirs; Name: "{app}\blender"
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Plynth"; Filename: "{app}\plynth.exe"

[UninstallDelete]
; Blender may leave compiled Python files in the scripts folder.
Type: filesandordirs; Name: "{app}\blender"

[Run]
Filename: "{app}\plynth.exe"; Description: "Start Plynth"; Flags: nowait postinstall skipifsilent

[Code]
const
  RuntimePage = 'https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist';

// The folder that goes on the user's command line path.
function CliFolder: String;
begin
  Result := ExpandConstant('{app}\cli');
end;

// Says whether one entry of the path names the given folder.
function SameFolder(Entry, Folder: String): Boolean;
begin
  Entry := Trim(Entry);
  if (Length(Entry) > 1) and (Entry[Length(Entry)] = '\') then
    Entry := Copy(Entry, 1, Length(Entry) - 1);
  Result := CompareText(Entry, Folder) = 0;
end;

// Says whether the user's Path value is stored as text that may hold %variables%.
function PathIsExpandable: Boolean;
var
  Output: TExecOutput;
  Code: Integer;
  I: Integer;
begin
  Result := True;
  if ExecAndCaptureOutput(ExpandConstant('{sys}\reg.exe'), 'query HKCU\Environment /v Path', '', SW_HIDE,
                          ewWaitUntilTerminated, Code, Output) and (Code = 0) then
    for I := 0 to GetArrayLength(Output.StdOut) - 1 do
      if Pos('REG_SZ', Output.StdOut[I]) > 0 then
        Result := False;
end;

// Writes the user's Path value back with the type it had.
procedure WriteUserPath(Value: String; Expandable: Boolean);
begin
  if Expandable then
    RegWriteExpandStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Value)
  else
    RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Value);
end;

// Adds the cli folder to the user's path once, keeping every other entry as it is.
procedure AddToPath;
var
  Value, Rest, Entry: String;
  Expandable: Boolean;
  P: Integer;
begin
  Expandable := PathIsExpandable;
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Value) then
    Value := '';
  Rest := Value;
  while Rest <> '' do begin
    P := Pos(';', Rest);
    if P = 0 then begin Entry := Rest; Rest := ''; end
    else begin Entry := Copy(Rest, 1, P - 1); Rest := Copy(Rest, P + 1, Length(Rest)); end;
    if SameFolder(Entry, CliFolder) then begin
      Log('The command line path already holds ' + CliFolder);
      exit;
    end;
  end;
  if (Value <> '') and (Value[Length(Value)] <> ';') then
    Value := Value + ';';
  WriteUserPath(Value + CliFolder, Expandable);
  Log('Added ' + CliFolder + ' to the command line path');
end;

// Removes exactly the cli folder from the user's path and keeps the rest in its order.
procedure RemoveFromPath;
var
  Value, Rest, Entry, Kept: String;
  Expandable, Removed, First: Boolean;
  P: Integer;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Value) then
    exit;
  Expandable := PathIsExpandable;
  Rest := Value;
  Kept := '';
  First := True;
  Removed := False;
  while Rest <> '' do begin
    P := Pos(';', Rest);
    if P = 0 then begin Entry := Rest; Rest := ''; end
    else begin Entry := Copy(Rest, 1, P - 1); Rest := Copy(Rest, P + 1, Length(Rest)); end;
    if SameFolder(Entry, CliFolder) then
      Removed := True
    else begin
      if not First then Kept := Kept + ';';
      Kept := Kept + Entry;
      First := False;
    end;
  end;
  if Removed then begin
    WriteUserPath(Kept, Expandable);
    Log('Removed ' + CliFolder + ' from the command line path');
  end;
end;

// Says whether the Microsoft C++ runtime is installed and new enough for these programs.
function RuntimeIsReady: Boolean;
var
  Installed, Major, Minor: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64, '{#RuntimeKey}', 'Installed', Installed) and (Installed = 1) and
            RegQueryDWordValue(HKLM64, '{#RuntimeKey}', 'Major', Major) and
            RegQueryDWordValue(HKLM64, '{#RuntimeKey}', 'Minor', Minor) and
            ((Major > {#RuntimeMajor}) or ((Major = {#RuntimeMajor}) and (Minor >= {#RuntimeMinor})));
end;

// After the files are in place: the path entry, then the runtime check, which never stops the install.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if CurStep <> ssPostInstall then
    exit;
#if PathEntry == "1"
  AddToPath;
#endif
  if RuntimeIsReady then
    exit;
  Log('The Microsoft Visual C++ runtime is missing or older than {#RuntimeMajor}.{#RuntimeMinor}. Page: ' + RuntimePage);
  if WizardSilent then
    exit;
  if MsgBox('Plynth needs the Microsoft Visual C++ Redistributable for Visual Studio 2015 to 2022 (x64), version ' +
            '{#RuntimeMajor}.{#RuntimeMinor} or newer. It is missing or older on this PC.' + #13#10#13#10 +
            'Open Microsoft''s download page now?', mbConfirmation, MB_YESNO) = IDYES then
    ShellExec('open', RuntimePage, '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

// After the files are removed: take the path entry out again. Settings, library and projects stay.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath;
end;
