@echo off
setlocal
cd /d "%~dp0"
call :install
set "POINT_INSTALL_RESULT=%errorlevel%"
if not "%POINT_INSTALL_RESULT%"=="0" echo Installation failed. Keep this window open and copy the error above.
if /i not "%~1"=="--no-pause" pause
exit /b %POINT_INSTALL_RESULT%

:install
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { exit 1 }"
if errorlevel 1 (
  echo Right-click install_hardened.bat and select Run as administrator.
  exit /b 1
)
if not exist build\Point.exe (
  echo Missing build\Point.exe. Extract the entire Point-Windows-x64 artifact first.
  exit /b 1
)
if not exist build\PointInstallCheck.exe (
  echo Missing build\PointInstallCheck.exe. Download a build made from the updated source.
  exit /b 1
)
set "POINT_POLICY_SOURCE=build\point-security.conf"
if not exist "%POINT_POLICY_SOURCE%" set "POINT_POLICY_SOURCE=point-security.conf"
if not exist "%POINT_POLICY_SOURCE%" (
  echo Missing point-security.conf in both build and the package root.
  exit /b 1
)
set "POINT_INSTALL_DIR=%ProgramW6432%\Point-Hardened"
if not defined ProgramW6432 set "POINT_INSTALL_DIR=%ProgramFiles%\Point-Hardened"
for %%D in (Inbox Workspace Exports Logs Staging Fetcher BrowserFetcher) do (
  if exist "%POINT_INSTALL_DIR%\%%D" (
    echo Refusing to change permissions over legacy data: %POINT_INSTALL_DIR%\%%D
    echo Move that data to the approved per-user data location first.
    exit /b 1
  )
)
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; if (Test-Path -LiteralPath $env:POINT_INSTALL_DIR) { $items=@(Get-Item -LiteralPath $env:POINT_INSTALL_DIR -Force)+@(Get-ChildItem -LiteralPath $env:POINT_INSTALL_DIR -Force -Recurse); foreach($item in $items) { if($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Linked installation path rejected: '+$item.FullName) } } }"
if errorlevel 1 exit /b 1
if not exist "%POINT_INSTALL_DIR%" mkdir "%POINT_INSTALL_DIR%"
if errorlevel 1 exit /b 1
for %%F in (Point.exe PointFetcher.exe PointBrowserFetcher.exe PointInstallCheck.exe) do (
  if exist "build\%%F" (
    copy /Y "build\%%F" "%POINT_INSTALL_DIR%\%%F"
    if errorlevel 1 exit /b 1
  )
)
if not exist "%POINT_INSTALL_DIR%\point-security.conf" (
  copy "%POINT_POLICY_SOURCE%" "%POINT_INSTALL_DIR%\point-security.conf"
  if errorlevel 1 exit /b 1
)
if exist scripts\point_xlsx_to_csv.ps1 (
  if not exist "%POINT_INSTALL_DIR%\scripts" mkdir "%POINT_INSTALL_DIR%\scripts"
  copy /Y scripts\point_xlsx_to_csv.ps1 "%POINT_INSTALL_DIR%\scripts\"
  if errorlevel 1 exit /b 1
)
rem Only this dedicated installation tree is changed. User data is separate.
icacls "%POINT_INSTALL_DIR%" /reset
if errorlevel 1 exit /b 1
icacls "%POINT_INSTALL_DIR%" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)RX"
if errorlevel 1 exit /b 1
icacls "%POINT_INSTALL_DIR%\*" /reset /T
if errorlevel 1 exit /b 1
icacls "%POINT_INSTALL_DIR%" /setowner "*S-1-5-32-544" /T
if errorlevel 1 exit /b 1
"%POINT_INSTALL_DIR%\PointInstallCheck.exe"
if errorlevel 1 exit /b 1
set "POINT_SHORTCUT=%PUBLIC%\Desktop\Point-Hardened.lnk"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -Command "$ErrorActionPreference='Stop'; $s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:POINT_SHORTCUT); $s.TargetPath=Join-Path $env:POINT_INSTALL_DIR 'Point.exe'; $s.WorkingDirectory=$env:POINT_INSTALL_DIR; $s.Save()"
if errorlevel 1 exit /b 1
echo Installation verified. Launch the Point-Hardened desktop shortcut normally.
echo If access is denied, provision your named account with configure_compliance.bat "DOMAIN\username" export.
echo Sign out and in after adding group membership. Do not launch the old Downloads copy.
exit /b 0
