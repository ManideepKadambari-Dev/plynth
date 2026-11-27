; Plynth installer for one user. Compile with ISCC and these defines:
;   /DAppVersion=0.1.0 /DStage=<staged folder> /DOutDir=<folder> /DRuntimeMajor=14 /DRuntimeMinor=44
; Optional: /DPathEntry=0 leaves the command line path alone, /DRuntimeKey=<key> points the runtime check
; at another registry key, for the install test. /DWaitSeconds=<n> shortens the wait for Plynth to close and
; /DTestBuild=1 compresses fast, both for test installers only. /DRunningMutex and /DSetupMutex pass the
; names from core/include/plynth/core/UpdateNames.h. The defaults below are the same names.
;
; Plynth starts its own update like this, and shipped copies depend on these switches:
;   PlynthSetup.exe /SILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /NOCLOSEAPPLICATIONS
;                   /NORESTARTAPPLICATIONS /PLYNTHUPDATE=1 [/PLYNTHREOPEN=1] /LOG=<file>
; An update never gets Blender. /MERGETASKS=!getblender turns the Blender tick box off for any other install.

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
#ifndef WaitSeconds
  #define WaitSeconds "60"
#endif
#ifndef TestBuild
  #define TestBuild "0"
#endif
#ifndef RunningMutex
  #define RunningMutex "PlynthRunning"
#endif
#ifndef SetupMutex
  #define SetupMutex "PlynthSetup"
#endif
; The folder "Get Blender for me" downloads into, the same name as downloadedBlenderRoot in
; core/src/blender/BlenderDownload.cpp.
#define DownloadedBlender "portable-blender"

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
#if TestBuild == "1"
Compression=lzma2/fast
SolidCompression=no
#else
Compression=lzma2/max
SolidCompression=yes
#endif
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

[Tasks]
; Ticked by default (owner, issue 37). plynth.com downloads nothing when a usable Blender is already found.
Name: "getblender"; Description: "Get Blender for me (about 400 MB from blender.org)"; GroupDescription: "Blender (skipped when a usable Blender is already on this PC):"

[InstallDelete]
; The program's own script and QML folders are replaced whole, so a file removed in a new version goes.
; The folder is fixed and checked for links before this runs.
Type: filesandordirs; Name: "{app}\blender"
Type: filesandordirs; Name: "{app}\qml"

[Files]
Source: "{#Stage}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Plynth"; Filename: "{app}\plynth.exe"

[UninstallDelete]
; Blender may leave compiled Python files in the scripts folder.
Type: filesandordirs; Name: "{app}\blender"
Type: filesandordirs; Name: "{app}\{#DownloadedBlender}"

[Run]
Filename: "{app}\plynth.exe"; Description: "Start Plynth"; Flags: nowait postinstall skipifsilent

[Code]
const
  RuntimePage = 'https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist';
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{8473EE0D-F68A-4C90-BB1C-1D2F3A293B12}_is1';
  AttrDirectory = $10;
  AttrReparsePoint = $400;
  GenericWrite = $40000000;
  OpenExisting = 3;
  InvalidHandle = $FFFFFFFF;
  ErrorFileNotFound = 2;
  ErrorPathNotFound = 3;
  ErrorAlreadyExists = 183;

var
  SetupMutexHandle: Cardinal;
  RuntimeRefused: Boolean;
  BlenderNotFetched: Boolean;

function CreateMutexW(Attributes: Cardinal; InitialOwner: Cardinal; Name: String): Cardinal;
  external 'CreateMutexW@kernel32.dll stdcall';
function CloseHandle(Handle: Cardinal): Cardinal;
  external 'CloseHandle@kernel32.dll stdcall';
function CreateFileW(Name: String; Access, Share, Security, Disposition, Flags, Template: Cardinal): Cardinal;
  external 'CreateFileW@kernel32.dll stdcall';

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

// Says whether Plynth started this run to update itself.
function IsUpdateMode: Boolean;
begin
  Result := ExpandConstant('{param:PLYNTHUPDATE|0}') = '1';
end;

// Says whether Plynth asked to be started again after a successful update.
function ReopenAsked: Boolean;
begin
  Result := ExpandConstant('{param:PLYNTHREOPEN|0}') = '1';
end;

// Makes the mutex that tells Plynth that setup runs. It is made with a handle that can be closed, because
// Plynth refuses to start while it exists. False when another setup already holds it.
function TakeSetupMutex: Boolean;
begin
  SetupMutexHandle := CreateMutexW(0, 0, '{#SetupMutex}');
  Result := (SetupMutexHandle <> 0) and (DLLGetLastError <> ErrorAlreadyExists);
  if (SetupMutexHandle <> 0) and not Result then begin
    CloseHandle(SetupMutexHandle);
    SetupMutexHandle := 0;
  end;
end;

// Lets go of the setup mutex, if this run holds it.
procedure ReleaseSetupMutex;
begin
  if SetupMutexHandle <> 0 then begin
    CloseHandle(SetupMutexHandle);
    SetupMutexHandle := 0;
  end;
end;

// Waits until no Plynth program or command runs, for the given number of seconds at most.
function WaitForPlynthToClose(Seconds: Integer): Boolean;
var
  I: Integer;
begin
  for I := 1 to Seconds * 2 do begin
    if not CheckForMutexes('{#RunningMutex}') then begin
      Result := True;
      exit;
    end;
    Sleep(500);
  end;
  Result := not CheckForMutexes('{#RunningMutex}');
end;

// Takes one number off the front of a version such as 0.1.0. -1 when it is not a number.
function TakeVersionPart(var Text: String): Integer;
var
  P: Integer;
begin
  P := Pos('.', Text);
  if P = 0 then begin
    Result := StrToIntDef(Text, -1);
    Text := '';
  end else begin
    Result := StrToIntDef(Copy(Text, 1, P - 1), -1);
    Text := Copy(Text, P + 1, Length(Text));
  end;
end;

// Compares two versions of the form major.minor.patch: below 0 when A is lower, 0 when they are equal,
// above 0 when A is higher. Unknown is set when either cannot be read.
function CompareVersions(A, B: String; var Unknown: Boolean): Integer;
var
  I, X, Y: Integer;
begin
  Result := 0;
  Unknown := False;
  for I := 1 to 3 do begin
    X := TakeVersionPart(A);
    Y := TakeVersionPart(B);
    if (X < 0) or (Y < 0) then begin
      Unknown := True;
      exit;
    end;
    if (Result = 0) and (X <> Y) then
      Result := X - Y;
  end;
  if (A <> '') or (B <> '') then
    Unknown := True;
end;

// Says whether a file or folder is a link to somewhere else, so nothing is replaced through it.
function IsReparsePoint(Path: String): Boolean;
var
  Rec: TFindRec;
begin
  Result := False;
  if FindFirst(Path, Rec) then begin
    Result := (Rec.Attributes and AttrReparsePoint) <> 0;
    FindClose(Rec);
  end;
end;

// Says whether another program has a file open, so it cannot be replaced now. A missing file is free.
function FileIsInUse(Path: String): Boolean;
var
  Handle, Error: Cardinal;
begin
  Handle := CreateFileW(Path, GenericWrite, 0, 0, OpenExisting, 0, 0);
  if Handle <> InvalidHandle then begin
    CloseHandle(Handle);
    Result := False;
  end else begin
    Error := DLLGetLastError;
    Result := (Error <> ErrorFileNotFound) and (Error <> ErrorPathNotFound);
  end;
end;

// The folder downloaded copies of Blender go into. Setup never replaces them, so it does not wait for them.
function DownloadedBlenderFolder: String;
begin
  Result := ExpandConstant('{app}\{#DownloadedBlender}');
end;

// Returns the first file below a folder that is in use, or an empty text when all are free. Links and the
// downloaded Blender are not followed.
function FirstFileInUse(Folder: String): String;
var
  Rec: TFindRec;
  Path: String;
begin
  Result := '';
  if not FindFirst(Folder + '\*', Rec) then
    exit;
  try
    repeat
      if (Rec.Name <> '.') and (Rec.Name <> '..') then begin
        Path := Folder + '\' + Rec.Name;
        if (Rec.Attributes and AttrDirectory) <> 0 then begin
          if ((Rec.Attributes and AttrReparsePoint) = 0) and (CompareText(Path, DownloadedBlenderFolder) <> 0) then
            Result := FirstFileInUse(Path);
        end else if FileIsInUse(Path) then
          Result := Path;
      end;
    until (Result <> '') or not FindNext(Rec);
  finally
    FindClose(Rec);
  end;
end;

// Waits until every installed file can be replaced, for the given number of seconds at most. Returns the
// file that is still in use, or an empty text.
function WaitForFreeFiles(Folder: String; Seconds: Integer): String;
var
  I: Integer;
begin
  for I := 1 to Seconds * 2 do begin
    Result := FirstFileInUse(Folder);
    if Result = '' then
      exit;
    Sleep(500);
  end;
  Result := FirstFileInUse(Folder);
end;

// Checks what an update needs before anything changes: Plynth installed in this fixed folder, a lower
// version installed, and a runtime new enough to start the new version. Returns the reason it cannot go on.
function UpdateRefusal: String;
var
  Location, Installed: String;
  Unknown: Boolean;
  Order: Integer;
begin
  Result := '';
  if not RegQueryStringValue(HKEY_CURRENT_USER, UninstallKey, 'InstallLocation', Location) or
     (CompareText(RemoveBackslashUnlessRoot(Location), ExpandConstant('{app}')) <> 0) or
     not FileExists(ExpandConstant('{app}\plynth.exe')) then begin
    Result := 'Plynth is not installed in ' + ExpandConstant('{app}') + ', so there is nothing to update. ' +
              'Install it with PlynthSetup.exe instead.';
    exit;
  end;
  if not RegQueryStringValue(HKEY_CURRENT_USER, UninstallKey, 'DisplayVersion', Installed) then
    Installed := '';
  Order := CompareVersions('{#AppVersion}', Installed, Unknown);
  if Unknown then begin
    Result := 'The installed version of Plynth could not be read, so it was not updated.';
    exit;
  end;
  if Order <= 0 then begin
    Result := 'Plynth ' + Installed + ' is installed. An update only installs a higher version, and this is ' +
              '{#AppVersion}.';
    exit;
  end;
  if not RuntimeIsReady then begin
    RuntimeRefused := True;
    Result := 'Plynth {#AppVersion} needs the Microsoft Visual C++ Redistributable for Visual Studio 2015 to ' +
              '2022 (x64), version {#RuntimeMajor}.{#RuntimeMinor} or newer. It is missing or older on this PC, ' +
              'so nothing was changed. Install it from ' + RuntimePage + ' and update again.';
  end;
end;

// Shows why an update stopped, when Plynth asked to be told. Plynth itself has already closed.
procedure ShowUpdateFailure(Reason: String);
var
  Code: Integer;
begin
  if not (IsUpdateMode and ReopenAsked) then
    exit;
  if RuntimeRefused then begin
    if MsgBox('The Plynth update did not run.' + #13#10#13#10 + Reason + #13#10#13#10 +
              'Open Microsoft''s download page now?', mbError, MB_YESNO) = IDYES then
      ShellExec('open', RuntimePage, '', '', SW_SHOWNORMAL, ewNoWait, Code);
  end else
    MsgBox('The Plynth update did not run.' + #13#10#13#10 + Reason, mbError, MB_OK);
end;

// Runs first. Plynth refuses to start from here on, and only one setup runs at a time.
function InitializeSetup: Boolean;
var
  Reason: String;
begin
  Result := TakeSetupMutex;
  if Result then
    exit;
  Reason := 'Another Plynth setup is running. Wait for it to finish, then try again.';
  Log(Reason);
  ShowUpdateFailure(Reason);
  if not WizardSilent then
    MsgBox(Reason, mbError, MB_OK);
end;

// Runs before the first file changes. Every check that can stop the install is here, so a refusal leaves
// the installed copy exactly as it was.
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  App, Busy: String;
begin
  App := ExpandConstant('{app}');
  Result := '';
  if IsUpdateMode then
    Result := UpdateRefusal;
  if (Result = '') and (IsReparsePoint(App) or IsReparsePoint(App + '\blender') or IsReparsePoint(App + '\qml')) then
    Result := 'The Plynth folder ' + App + ' or a folder in it is a link to another place, so nothing was changed.';
  if (Result = '') and not WaitForPlynthToClose({#WaitSeconds}) then
    Result := 'Plynth is still running. Close every Plynth window and command, then try again.';
  if (Result = '') and DirExists(App) then begin
    Busy := WaitForFreeFiles(App, {#WaitSeconds});
    if Busy <> '' then
      Result := Busy + ' is in use by another program, so nothing was changed. Close it, then try again.';
  end;
  if Result <> '' then begin
    Log('Stopped before any change: ' + Result);
    ShowUpdateFailure(Result);
  end;
end;

// Reads the value of one key from a line of JSON as text: a string without its quotes, or a whole number.
// Empty when the key is not there.
function JsonValue(Line, Key: String): String;
var
  P, I: Integer;
begin
  Result := '';
  P := Pos('"' + Key + '":', Line);
  if P = 0 then
    exit;
  I := P + Length(Key) + 3;
  while (I <= Length(Line)) and (Line[I] = ' ') do
    I := I + 1;
  if (I <= Length(Line)) and (Line[I] = '"') then begin
    I := I + 1;
    while (I <= Length(Line)) and (Line[I] <> '"') do begin
      if (Line[I] = '\') and (I < Length(Line)) then
        I := I + 1;
      Result := Result + Line[I];
      I := I + 1;
    end;
  end else
    while (I <= Length(Line)) and (Pos(Line[I], '-0123456789') > 0) do begin
      Result := Result + Line[I];
      I := I + 1;
    end;
end;

// Shows one progress line of "plynth blender get" on the installing page.
procedure ShowBlenderProgress(Line: String);
var
  Done, Total: Int64;
  Permille: Integer;
begin
  if JsonValue(Line, 'message') <> '' then
    WizardForm.StatusLabel.Caption := JsonValue(Line, 'message');
  Done := StrToInt64Def(JsonValue(Line, 'done'), 0);
  Total := StrToInt64Def(JsonValue(Line, 'total'), 0);
  if (Total <= 0) or (Done < 0) then begin
    WizardForm.ProgressGauge.Style := npbstMarquee;
    WizardForm.FilenameLabel.Caption := '';
    exit;
  end;
  if Done > Total then
    Done := Total;
  Permille := Done * 1000 div Total;
  WizardForm.ProgressGauge.Style := npbstNormal;
  WizardForm.ProgressGauge.Max := 1000;
  WizardForm.ProgressGauge.Position := Permille;
  if JsonValue(Line, 'stage') = 'downloading' then
    WizardForm.FilenameLabel.Caption := IntToStr(Done div 1048576) + ' of ' + IntToStr(Total div 1048576) + ' MB'
  else
    WizardForm.FilenameLabel.Caption := '';
end;

// Takes each line "plynth blender get" writes: progress lines go on the page, everything else in the log.
procedure OnBlenderOutput(const S: String; const Error, FirstLine: Boolean);
begin
  if Error and (Pos('"progress"', S) > 0) then
    ShowBlenderProgress(S)
  else if Trim(S) <> '' then
    Log('plynth blender get: ' + S);
end;

// Runs "plynth blender get --yes --if-missing" and says whether it ended well. Exit code 0 means Blender was
// downloaded or a usable one was already there.
function RunBlenderGet: Boolean;
var
  Code: Integer;
begin
  Result := False;
  WizardForm.StatusLabel.Caption := 'Getting Blender from blender.org (about 400 MB). This can take several minutes.';
  WizardForm.FilenameLabel.Caption := '';
  WizardForm.ProgressGauge.Style := npbstMarquee;
  // plynth.com refuses to start while the setup mutex exists. Every file is in place by now.
  ReleaseSetupMutex;
  try
    if not ExecAndLogOutput(ExpandConstant('{app}\plynth.com'), 'blender get --yes --if-missing',
                            ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Code, @OnBlenderOutput) then
      Log('plynth.com could not be started to get Blender: ' + SysErrorMessage(Code))
    else if Code <> 0 then
      Log('Getting Blender ended with exit code ' + IntToStr(Code))
    else
      Result := True;
  except
    Log('Getting Blender stopped: ' + GetExceptionMessage);
  end;
  if not TakeSetupMutex then
    Log('The setup mutex could not be taken again after getting Blender.');
  WizardForm.ProgressGauge.Style := npbstNormal;
  WizardForm.ProgressGauge.Position := WizardForm.ProgressGauge.Max;
  WizardForm.FilenameLabel.Caption := '';
end;

// Gets Blender when the tick box asks for it. Never during an update, and it never stops the install: when it
// does not work, the last page says so in one sentence.
procedure GetBlenderIfAsked;
begin
  if IsUpdateMode then begin
    Log('An update does not get Blender.');
    exit;
  end;
  if not WizardIsTaskSelected('getblender') then
    exit;
  if not RuntimeIsReady then begin
    Log('Blender was not downloaded, because Plynth cannot run without the Microsoft Visual C++ runtime.');
    BlenderNotFetched := True;
    exit;
  end;
  BlenderNotFetched := not RunBlenderGet;
end;

// Adds the note about Blender to the last page when getting it did not work.
procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID <> wpFinished) or not BlenderNotFetched then
    exit;
  WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
    'Blender could not be downloaded now, but you can get it later from the setup check in Plynth.';
  WizardForm.RunList.Top := WizardForm.RunList.Top + WizardForm.AdjustLabelHeight(WizardForm.FinishedLabel);
end;

// After the files are in place: the path entry, then the runtime check, which never stops a normal install.
// At the very end Plynth is started again when it asked for that.
procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
begin
  if CurStep = ssDone then begin
    if IsUpdateMode and ReopenAsked then begin
      // The new Plynth refuses to start while the setup mutex exists.
      ReleaseSetupMutex;
      if not Exec(ExpandConstant('{app}\plynth.exe'), '', ExpandConstant('{app}'), SW_SHOWNORMAL, ewNoWait, Code) then
        Log('Plynth could not be started again: ' + SysErrorMessage(Code));
    end;
    exit;
  end;
  if CurStep <> ssPostInstall then
    exit;
#if PathEntry == "1"
  AddToPath;
#endif
  GetBlenderIfAsked;
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

// Runs last, also after a refusal.
procedure DeinitializeSetup;
begin
  ReleaseSetupMutex;
end;

// Runs first in the uninstaller: the same mutex and the same wait as setup.
function InitializeUninstall: Boolean;
var
  Reason: String;
begin
  Result := TakeSetupMutex;
  if not Result then
    Reason := 'A Plynth setup is running. Wait for it to finish, then try again.'
  else begin
    Result := WaitForPlynthToClose({#WaitSeconds});
    if not Result then
      Reason := 'Plynth is still running. Close every Plynth window and command, then try again.';
  end;
  if Result then
    exit;
  Log(Reason);
  if not UninstallSilent then
    MsgBox(Reason, mbError, MB_OK);
end;

// After the files are removed: take the path entry out again. Settings, library and projects stay.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath;
end;

// Runs last in the uninstaller.
procedure DeinitializeUninstall;
begin
  ReleaseSetupMutex;
end;
