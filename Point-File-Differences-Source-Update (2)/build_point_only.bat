@echo off
setlocal
cd /d "%~dp0"
call test_file_differences.bat
if errorlevel 1 exit /b 1
if not exist build mkdir build
rc /nologo /fo build\point.res src\point.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /sdl /guard:cf /DUNICODE /D_UNICODE /O2 /MT ^
  src\point_core.cpp src\point_compliance.cpp src\point_excel_import.cpp src\point_win32.cpp ^
  build\point.res /Fe:build\Point.exe /link /DYNAMICBASE /NXCOMPAT /GUARD:CF ^
  user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib oleaut32.lib ^
  advapi32.lib crypt32.lib bcrypt.lib
if errorlevel 1 exit /b 1
if not exist build\point-security.conf copy /Y point-security.conf build\ >nul
if not exist build\Inbox mkdir build\Inbox
if exist scripts\point_xlsx_to_csv.ps1 (
  if not exist build\scripts mkdir build\scripts
  copy /Y scripts\point_xlsx_to_csv.ps1 build\scripts\ >nul
)
echo Built Point.exe. XLSX import requires your trusted signed reader; CSV works without that helper.

cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /sdl /guard:cf /DUNICODE /D_UNICODE /O2 /MT ^
  src\point_compliance.cpp src\point_install_check.cpp /Fe:build\PointInstallCheck.exe ^
  /link /DYNAMICBASE /NXCOMPAT /GUARD:CF advapi32.lib crypt32.lib shell32.lib ole32.lib netapi32.lib uuid.lib user32.lib
if errorlevel 1 exit /b 1
