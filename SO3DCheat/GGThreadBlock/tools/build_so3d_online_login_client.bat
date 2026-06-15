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

rem Data tables + global defaults live next to the exe (APP_DIR); copy latest into dist each build.
if not exist "item_names.json"          echo [warn] missing data table: item_names.json
if not exist "map_id_table.json"        echo [warn] missing data table: map_id_table.json
if not exist "map_names.json"           echo [warn] missing data table: map_names.json
if not exist "so3d_online_defaults.json" echo [warn] missing config: so3d_online_defaults.json
if exist "item_names.json"          copy /Y "item_names.json"          "dist\item_names.json"          >nul
if exist "map_id_table.json"        copy /Y "map_id_table.json"        "dist\map_id_table.json"        >nul
if exist "map_names.json"           copy /Y "map_names.json"           "dist\map_names.json"           >nul
if exist "so3d_online_defaults.json" copy /Y "so3d_online_defaults.json" "dist\so3d_online_defaults.json" >nul

rem so3d_online_accounts.json holds passwords - NOT bundled. Put it next to the exe yourself.
if not exist "dist\so3d_online_logs" mkdir "dist\so3d_online_logs"

echo [build] done: %CD%\dist\so3d_online_login_client.exe
echo [build] bundled: item_names.json / map_id_table.json / map_names.json / so3d_online_defaults.json
echo [build] NOTE: account config NOT bundled - put so3d_online_accounts.json next to the exe yourself.
pause
