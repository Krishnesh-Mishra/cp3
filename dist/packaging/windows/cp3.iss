; =============================================================================
; cp3.iss - Inno Setup 6 script for the per-user cp3 installer.
;
; Built by dist/packaging/windows/build-installer.ps1, which passes the payload
; paths on the ISCC command line:
;   /DStageDir=<absolute stage root produced by package.ps1>
;   /DVersion=<packaging\VERSION>
;   /DOutputDir=<directory that receives the setup exe>
;   /DVsixName=<file name of the bundled .vsix>   (optional; omitted when absent)
;
; Installer behaviour (PKG-CONTRACTS section 4):
;   * per-user, default %LOCALAPPDATA%\Programs\cp3, no admin required;
;   * copies the staged layout verbatim;
;   * appends <app>\bin to the USER PATH (HKCU\Environment), idempotently,
;     and removes that entry again on uninstall without disturbing any other
;     entry (empty entries included), so the value round-trips byte-exactly;
;     the registry type follows the content (REG_EXPAND_SZ when the value
;     uses %variables%, REG_SZ otherwise); the system PATH is never touched;
;   * installs the bundled VS Code extension with `code --install-extension`
;     when the `code` CLI exists, otherwise points at `cp3 editor-plugin`;
;   * quiet modes: Inno's /SILENT and /VERYSILENT (plus /SUPPRESSMSGBOXES);
;   * no-side-effect test mode: /TESTMODE=1 (same as /NOPATH=1 /NOVSCODE=1)
;     copies files only and never touches PATH or VS Code.
;
; Every switch is documented in dist/packaging/windows/README.md.
; =============================================================================

#ifndef StageDir
  #error StageDir is not defined; run dist/packaging/windows/build-installer.ps1
#endif
#ifndef Version
  #error Version is not defined; run dist/packaging/windows/build-installer.ps1
#endif
#ifndef OutputDir
  #error OutputDir is not defined; run dist/packaging/windows/build-installer.ps1
#endif

[Setup]
AppId={{8F3C2B7A-5D64-4E1B-9C0A-7E2F1B4D6A93}
AppName=cp3
AppVersion={#Version}
AppVerName=cp3 {#Version}
AppPublisher=cp3
AppComments=cp3 C+++ compiler, runtime and tooling
DefaultDirName={localappdata}\Programs\cp3
DefaultGroupName=cp3
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
OutputDir={#OutputDir}
OutputBaseFilename=cp3-setup-{#Version}-x64
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupLogging=yes
UninstallDisplayName=cp3 {#Version}
UninstallDisplayIcon={app}\bin\cp3c.exe

[Files]
Source: "{#StageDir}\bin\cp3c.exe"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#StageDir}\bin\cp3.cmd"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#StageDir}\bin\*.dll"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#StageDir}\include\*"; DestDir: "{app}\include"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\src\*"; DestDir: "{app}\src"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\third_party\*"; DestDir: "{app}\third_party"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\manifest.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\README.txt"; DestDir: "{app}"; Flags: ignoreversion
#ifdef VsixName
Source: "{#StageDir}\share\vscode\{#VsixName}"; DestDir: "{app}\share\vscode"; Flags: ignoreversion
#endif

[Code]

const
  EnvironmentKey = 'Environment';
  PathValueName = 'Path';

{ True when the command line carries /Name or /Name=<value other than 0>. }
function CmdLineSwitch(const Name: String): Boolean;
begin
  Result := ExpandConstant('{param:' + Name + '|0}') <> '0';
end;

{ /TESTMODE=1 makes the whole install side-effect free. }
function TestMode(): Boolean;
begin
  Result := CmdLineSwitch('TESTMODE');
end;

function PathEditsEnabled(): Boolean;
begin
  Result := (not TestMode()) and (not CmdLineSwitch('NOPATH'));
end;

function VsCodeInstallEnabled(): Boolean;
begin
  Result := (not TestMode()) and (not CmdLineSwitch('NOVSCODE'));
end;

function BinDir(): String;
begin
  Result := ExpandConstant('{app}\bin');
end;

{ Case-insensitive, boundary-aware membership test for an entry in a
  semicolon-separated PATH value. }
function PathContainsDir(const PathValue, Dir: String): Boolean;
var
  Lower, Needle: String;
begin
  Lower := Lowercase(PathValue);
  Needle := Lowercase(Dir);
  Result := (Lower = Needle) or (Pos(';' + Needle + ';', ';' + Lower + ';') > 0);
end;

{ Writes a PATH value back with the registry type its content needs:
  REG_EXPAND_SZ when it contains %variables% (so they keep expanding),
  REG_SZ otherwise. Keeping REG_SZ for %-free values means an install and
  uninstall round trip leaves the value's registry type untouched too. }
function WritePathValue(const Value: String): Boolean;
begin
  if Pos('%', Value) > 0 then
    Result := RegWriteExpandStringValue(HKCU, EnvironmentKey, PathValueName, Value)
  else
    Result := RegWriteStringValue(HKCU, EnvironmentKey, PathValueName, Value);
end;

{ Appends <app>\bin to HKCU\Environment Path when it is not there yet. The
  separator is always ';' + Dir - even when the old value ends with ';' - so
  RemovePathEntry() below is an exact inverse and empty PATH entries survive
  the install/uninstall round trip. }
procedure AddToUserPath();
var
  OldPath, NewPath: String;
begin
  if not PathEditsEnabled() then
  begin
    Log('PATH: not touched (test mode or /NOPATH)');
    Exit;
  end;
  if not RegQueryStringValue(HKCU, EnvironmentKey, PathValueName, OldPath) then
  begin
    Log('PATH: HKCU\Environment has no Path value yet; creating it');
    OldPath := '';
  end;
  if PathContainsDir(OldPath, BinDir()) then
  begin
    Log('PATH: already contains ' + BinDir() + '; nothing to do');
    Exit;
  end;
  if OldPath <> '' then
    NewPath := OldPath + ';' + BinDir()
  else
    NewPath := BinDir();
  if WritePathValue(NewPath) then
    Log('PATH: appended ' + BinDir() + ' (open a new terminal to pick it up)')
  else
    SuppressibleMsgBox('cp3 could not update the user PATH.' + #13#10 +
      'Add "' + BinDir() + '" to PATH manually or re-run the installer.',
      mbError, MB_OK, IDOK);
end;

{ Rebuilds a PATH value with every entry equal to Dir (case-insensitively)
  dropped. All other entries - including empty ones - keep their order and
  separators, so removing the appended <app>\bin yields the exact original
  string back. }
function RemovePathEntry(const PathValue, Dir: String): String;
var
  Rest, Item, Acc: String;
  P: Integer;
  First: Boolean;
begin
  Acc := '';
  First := True;
  Rest := PathValue;
  repeat
    P := Pos(';', Rest);
    if P = 0 then
    begin
      Item := Rest;
      Rest := '';
    end
    else
    begin
      Item := Copy(Rest, 1, P - 1);
      Rest := Copy(Rest, P + 1, Length(Rest) - P);
    end;
    if CompareText(Item, Dir) <> 0 then
    begin
      if not First then
        Acc := Acc + ';';
      Acc := Acc + Item;
    end;
    First := False;
  until P = 0;
  Result := Acc;
end;

procedure RemoveFromUserPath();
var
  OldPath, NewPath: String;
begin
  if not RegQueryStringValue(HKCU, EnvironmentKey, PathValueName, OldPath) then
    Exit;
  if not PathContainsDir(OldPath, BinDir()) then
  begin
    Log('uninstall: PATH has no entry for ' + BinDir());
    Exit;
  end;
  NewPath := RemovePathEntry(OldPath, BinDir());
  if WritePathValue(NewPath) then
    Log('uninstall: removed ' + BinDir() + ' from the user PATH')
  else
    Log('uninstall: could not update the user PATH');
end;

{ The `code` CLI: PATH first, then the standard VS Code install locations. }
function FindCodeCli(): String;
var
  Candidate: String;
begin
  Result := FileSearch('code.cmd', GetEnv('PATH'));
  if Result = '' then
    Result := FileSearch('code.exe', GetEnv('PATH'));
  if Result <> '' then
    Exit;
  Candidate := ExpandConstant('{localappdata}\Programs\Microsoft VS Code\bin\code.cmd');
  if FileExists(Candidate) then
  begin
    Result := Candidate;
    Exit;
  end;
  Candidate := ExpandConstant('{pf}\Microsoft VS Code\bin\code.cmd');
  if FileExists(Candidate) then
  begin
    Result := Candidate;
    Exit;
  end;
  Candidate := ExpandConstant('{pf32}\Microsoft VS Code\bin\code.cmd');
  if FileExists(Candidate) then
  begin
    Result := Candidate;
    Exit;
  end;
end;

procedure InstallBundledExtension();
#ifdef VsixName
var
  CodeCli, Vsix, Params: String;
  ResultCode: Integer;
#endif
begin
#ifdef VsixName
  if not VsCodeInstallEnabled() then
  begin
    Log('VS Code extension: not installed (test mode or /NOVSCODE)');
    Exit;
  end;
  Vsix := ExpandConstant('{app}\share\vscode\{#VsixName}');
  CodeCli := FindCodeCli();
  if CodeCli = '' then
  begin
    Log('VS Code extension: the `code` CLI was not found');
    if not WizardSilent then
      MsgBox('VS Code was not found, so the bundled cp3 extension was not installed.' + #13#10 +
        'Install VS Code, then run: cp3 editor-plugin', mbInformation, MB_OK);
    Exit;
  end;
  Params := '/C ""' + CodeCli + '" --install-extension "' + Vsix + '" --force"';
  if Exec(ExpandConstant('{cmd}'), Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Log('VS Code extension: code --install-extension exited with ' + IntToStr(ResultCode));
    if (ResultCode <> 0) and (not WizardSilent) then
      MsgBox('Installing the cp3 extension into VS Code failed (exit code ' +
        IntToStr(ResultCode) + ').' + #13#10 +
        'Run `cp3 editor-plugin` to retry.', mbError, MB_OK);
  end
  else
    Log('VS Code extension: could not launch ' + CodeCli);
#else
  Log('VS Code extension: none bundled in this package');
#endif
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    AddToUserPath();
    InstallBundledExtension();
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
    RemoveFromUserPath();
end;
