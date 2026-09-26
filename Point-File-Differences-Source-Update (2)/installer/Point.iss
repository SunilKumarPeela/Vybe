[Setup]
AppId=PointHardenedDesktop
AppName=Point
AppVersion=8.32.2
AppPublisher=Point
DefaultDirName={autopf}\Point-Hardened
UsePreviousAppDir=no
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\build-installer
OutputBaseFilename=Point-Hardened-8.32.2-Setup
SetupIconFile=..\point.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\Point.exe
SetupLogging=yes
CloseApplications=yes
CloseApplicationsFilter=*.exe,*.dll
RestartApplications=no

[Files]
Source: "..\build\Point.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\PointFetcher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\PointBrowserFetcher.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\PointInstallCheck.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\point-security.conf"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "..\build\PointInstallCheck.exe"; DestName: "PointSetupHelper.exe"; Flags: dontcopy
#if FileExists("..\scripts\point_xlsx_to_csv.ps1")
Source: "..\scripts\point_xlsx_to_csv.ps1"; DestDir: "{app}\scripts"; Flags: ignoreversion
#endif

[Icons]
Name: "{commonprograms}\Point-Hardened"; Filename: "{app}\Point.exe"
Name: "{commondesktop}\Point-Hardened"; Filename: "{app}\Point.exe"

[Code]
var
  AccountPage: TInputQueryWizardPage;
  ExportBox: TNewCheckBox;
  DetectedAccount: Boolean;

procedure InitializeWizard;
var
  Code: Integer;
  AccountText: AnsiString;
begin
  AccountPage := CreateInputQueryPage(wpWelcome, 'Who will use Point?',
    'Choose the Windows account that will use this app.',
    'Enter COMPUTER\username or DOMAIN\username. If IT is installing for someone else, enter that person''s account.');
  AccountPage.Add('Windows account:', False);
  AccountPage.Values[0] := ExpandConstant('{param:POINTUSER|}');
  DetectedAccount := False;
  if AccountPage.Values[0] = '' then begin
    ExtractTemporaryFile('PointSetupHelper.exe');
    try
      if ExecAsOriginalUser(ExpandConstant('{tmp}\PointSetupHelper.exe'),
        '--detect-account "' + ExpandConstant('{tmp}\point-original-account.txt') + '"',
        '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
        if Code = 0 then begin
          if LoadStringFromFile(ExpandConstant('{tmp}\point-original-account.txt'), AccountText) then begin
            AccountPage.Values[0] := Trim(UTF8Decode(AccountText));
            DetectedAccount := AccountPage.Values[0] <> '';
          end;
        end;
      end;
    except
      Log('Original account detection unavailable; showing account selection.');
    end;
  end;
  ExportBox := TNewCheckBox.Create(AccountPage);
  ExportBox.Parent := WizardForm.ReadyPage;
  WizardForm.ReadyMemo.Height := WizardForm.ReadyMemo.Height - ScaleY(32);
  ExportBox.Top := WizardForm.ReadyMemo.Top + WizardForm.ReadyMemo.Height + ScaleY(8);
  ExportBox.Left := WizardForm.ReadyMemo.Left;
  ExportBox.Width := WizardForm.ReadyMemo.Width;
  ExportBox.Caption := 'Allow this account to copy and export results';
  ExportBox.Checked := ExpandConstant('{param:POINTEXPORT|0}') = '1';
  WizardForm.FinishedLabel.Caption :=
    'Point is installed. Sign out of Windows and sign back in once to activate access, then open the Point-Hardened desktop shortcut.';
end;

function ValidAccount: Boolean;
var
  Code: Integer;
  Account: String;
begin
  Account := Trim(AccountPage.Values[0]);
  Result := False;
  if (Account = '') or (Pos('"', Account) > 0) or
     (Pos(#13, Account) > 0) or (Pos(#10, Account) > 0) then Exit;
  if not FileExists(ExpandConstant('{tmp}\PointSetupHelper.exe')) then
    ExtractTemporaryFile('PointSetupHelper.exe');
  if Exec(ExpandConstant('{tmp}\PointSetupHelper.exe'),
    '--validate-account "' + Account + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
    Result := Code = 0;
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = AccountPage.ID) and DetectedAccount;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := 'Point will be installed and configured automatically for:' + NewLine +
    Trim(AccountPage.Values[0]) + NewLine + NewLine +
    'A desktop shortcut will open the correct installed copy.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = AccountPage.ID then begin
    Result := ValidAccount;
    if not Result then MsgBox('Windows could not find that user. Enter COMPUTER\username or DOMAIN\username.', mbError, MB_OK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Result := '';
  if CompareText(RemoveBackslashUnlessRoot(ExpandConstant('{app}')),
    RemoveBackslashUnlessRoot(ExpandConstant('{autopf}\Point-Hardened'))) <> 0 then begin
    Result := 'Point must use its protected Program Files folder. Remove any custom /DIR option.';
    Exit;
  end;
  if not ValidAccount then begin
    Result := 'Choose a valid Windows user account before installing Point.';
    Exit;
  end;
  if not Exec(ExpandConstant('{tmp}\PointSetupHelper.exe'), '--preflight-install',
    '', SW_HIDE, ewWaitUntilTerminated, Code) then begin
    Result := 'Could not inspect the installation folder.';
    Exit;
  end;
  if Code <> 0 then Result := 'The installation folder contains linked or unreadable files. Ask your administrator to repair it.';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  Params, Mode: String;
begin
  if CurStep = ssPostInstall then begin
    Params := '--secure-installed';
    if not Exec(ExpandConstant('{tmp}\PointSetupHelper.exe'), Params,
      '', SW_HIDE, ewWaitUntilTerminated, Code) then
      RaiseException('Windows could not verify the installed permissions.');
    if Code <> 0 then
      RaiseException('Installation permissions could not be verified. Setup cannot finish safely. Contact your administrator.');
    Mode := 'view';
    if ExportBox.Checked then Mode := 'export';
    if not Exec(ExpandConstant('{app}\PointInstallCheck.exe'),
      '--provision "' + Trim(AccountPage.Values[0]) + '" ' + Mode,
      '', SW_HIDE, ewWaitUntilTerminated, Code) then
      RaiseException('Windows could not configure Point access.');
    if Code <> 0 then RaiseException('Point access could not be configured. Windows helper exit code: ' + IntToStr(Code));
    try
      if not ExecAsOriginalUser(ExpandConstant('{app}\PointInstallCheck.exe'),
        '--refresh-shortcuts', '', SW_HIDE, ewWaitUntilTerminated, Code) then
        Log('Old shortcuts could not be updated; use the new Point-Hardened shortcut.');
    except
      Log('Old shortcut update unavailable; use the new Point-Hardened shortcut.');
    end;
  end;
end;
