@echo off
setlocal
if "%~1"=="" (
  echo Usage: configure_compliance.bat "DOMAIN\username" [export]
  echo Name the intended Point user explicitly. Add export only to grant copying and CSV export.
  exit /b 1
)
net session >nul 2>&1
if errorlevel 1 (
  echo Run this script from an Administrator Command Prompt.
  exit /b 1
)
net localgroup "Point Users" >nul 2>&1
if errorlevel 1 net localgroup "Point Users" /add
if errorlevel 1 exit /b 1
net localgroup "Point Administrators" >nul 2>&1
if errorlevel 1 net localgroup "Point Administrators" /add
if errorlevel 1 exit /b 1
net localgroup "Point Exporters" >nul 2>&1
if errorlevel 1 net localgroup "Point Exporters" /add
if errorlevel 1 exit /b 1
net localgroup "Point Users" "%~1" /add
if errorlevel 1 (
  echo Group addition failed or this account is already a member. Verify the displayed result.
  exit /b 1
)
if /i "%~2"=="export" (
  net localgroup "Point Exporters" "%~1" /add
  if errorlevel 1 exit /b 1
)
echo Access configured for the named account. Sign out and sign in again to update its Windows token.
