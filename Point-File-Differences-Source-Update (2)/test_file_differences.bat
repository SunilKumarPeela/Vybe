@echo off
setlocal
cd /d "%~dp0"
if not exist build-tests mkdir build-tests
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /sdl /DUNICODE /D_UNICODE /Od /Zi ^
  /Isrc /c src\point_core.cpp /Fo:build-tests\point_core.obj
if errorlevel 1 exit /b 1
  if not errorlevel 0 exit /b 1
for %%T in (file_diff_test reconcile_test security_regression_test) do (
  cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /sdl /DUNICODE /D_UNICODE /Od /Zi ^
    /Isrc tests\%%T.cpp build-tests\point_core.obj /Fe:build-tests\%%T.exe /link bcrypt.lib
  if errorlevel 1 exit /b 1
  if not errorlevel 0 exit /b 1
  call :run_test "build-tests\%%T.exe" tests\fixtures
  if errorlevel 1 exit /b 1
  if not errorlevel 0 exit /b 1
)
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /sdl /DUNICODE /D_UNICODE /Od /Zi ^
  /Isrc src\point_compliance.cpp tests\windows_security_test.cpp /Fe:build-tests\windows_security_test.exe ^
  /link advapi32.lib crypt32.lib shell32.lib ole32.lib
if errorlevel 1 exit /b 1
  if not errorlevel 0 exit /b 1
call :run_test "build-tests\windows_security_test.exe"
if errorlevel 1 exit /b 1
  if not errorlevel 0 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /WX /sdl /DUNICODE /D_UNICODE /c ^
  /Isrc tests\file_diff_ui_compile.cpp /Fo:build-tests\file_diff_ui_compile.obj
exit /b %errorlevel%

:run_test
echo Running %~1
%*
set "POINT_TEST_EXIT=%errorlevel%"
if not "%POINT_TEST_EXIT%"=="0" (
  echo ERROR: %~1 exited with code %POINT_TEST_EXIT%.
  exit /b 1
)
exit /b 0
