@echo off
setlocal

cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
  echo [error] python not found in PATH.
  pause
  exit /b 1
)

python -m PyInstaller --version >nul 2>nul
if errorlevel 1 (
  echo [build] PyInstaller not found, installing...
  python -m pip install --upgrade pyinstaller
  if errorlevel 1 (
    echo [error] failed to install PyInstaller.
    pause
    exit /b 1
  )
)

echo [build] building so3d_online_login_client.exe...
python -m PyInstaller ^
  --noconfirm ^
  --clean ^
  --onefile ^
  --windowed ^
  --name so3d_online_login_client ^
  --collect-submodules Crypto ^
  so3d_online_login_client.py

if errorlevel 1 (
  echo [error] build failed.
  pause
  exit /b 1
)

if not exist "dist" mkdir "dist"
if not exist "dist\so3d_online_accounts.json" (
  copy /Y "so3d_online_accounts.json" "dist\so3d_online_accounts.json" >nul
)
if not exist "dist\so3d_online_logs" mkdir "dist\so3d_online_logs"

echo [build] done: %CD%\dist\so3d_online_login_client.exe
echo [build] config: %CD%\dist\so3d_online_accounts.json
pause
