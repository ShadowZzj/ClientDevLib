@echo off
setlocal

rem This script lives in so3d_online_client\. The exe still goes to ..\dist (tools\dist), unchanged.
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
rem Entry script statically imports so3dclient.cli, so the whole package
rem (farm subpackage + bridge_client) is traced and bundled automatically.
python -m PyInstaller ^
  --noconfirm ^
  --clean ^
  --onefile ^
  --windowed ^
  --name so3d_online_login_client ^
  --collect-submodules Crypto ^
  --paths . ^
  --distpath ..\dist ^
  --workpath ..\build ^
  --specpath ..\build ^
  so3d_online_login_client.py

if errorlevel 1 (
  echo [error] build failed.
  pause
  exit /b 1
)

rem port_proxy.exe : standalone proxy worker (loads protect.dll, does node-pin/auto-claim).
rem Built as its own onefile exe so the delivered dist needs NO python on the target machine.
rem Console build: proxy_pool spawns it in a new console so its [bind]/[Selector] logs are visible.
rem No --clean here (would wipe the bot exe's workpath cache); stdlib-only, no extra collects.
echo [build] building port_proxy.exe (standalone proxy worker, no python needed on target)...
python -m PyInstaller ^
  --noconfirm ^
  --onefile ^
  --console ^
  --name port_proxy ^
  --paths . ^
  --distpath ..\dist ^
  --workpath ..\build ^
  --specpath ..\build ^
  port_proxy.py

if errorlevel 1 (
  echo [error] port_proxy build failed.
  pause
  exit /b 1
)

if not exist "..\dist" mkdir "..\dist"

rem Data tables + global defaults are read at runtime from the exe dir (APP_DIR).
rem Copy the latest copies next to the exe on every build.
if not exist "item_names.json"           echo [warn] missing data table: item_names.json
if not exist "map_id_table.json"         echo [warn] missing data table: map_id_table.json
if not exist "map_names.json"            echo [warn] missing data table: map_names.json
if not exist "monster_names.json"        echo [warn] missing data table: monster_names.json
if not exist "so3d_online_defaults.json" echo [warn] missing config: so3d_online_defaults.json
if exist "item_names.json"           copy /Y "item_names.json"           "..\dist\item_names.json"           >nul
if exist "map_id_table.json"         copy /Y "map_id_table.json"         "..\dist\map_id_table.json"         >nul
if exist "map_names.json"            copy /Y "map_names.json"            "..\dist\map_names.json"            >nul
if exist "monster_names.json"        copy /Y "monster_names.json"        "..\dist\monster_names.json"        >nul
if exist "so3d_online_defaults.json" copy /Y "so3d_online_defaults.json" "..\dist\so3d_online_defaults.json" >nul

rem Auto egress-routing assets (read at runtime from APP_DIR = exe dir):
rem  - clash\mihomo.exe + wintun.dll : mihomo TUN core (clash_auto looks in APP_DIR\clash\).
rem    Only the two binaries are shipped; mihomo rebuilds cache.db / mihomo.pid at runtime.
rem  - port_proxy.exe : built above (standalone, no python on target). proxy_pool runs
rem    APP_DIR\port_proxy.exe; for SOCKS5 lanes it hardlinks it to ggs5exit{k}.exe for mihomo routing.
rem  - protect.dll : loaded by port_proxy.exe at runtime from the exe dir - must sit next to it.
if not exist "clash\mihomo.exe" echo [warn] missing clash core: clash\mihomo.exe (auto egress routing will be disabled)
if not exist "clash\wintun.dll" echo [warn] missing TUN driver: clash\wintun.dll (auto egress routing will be disabled)
if not exist "protect.dll"      echo [warn] missing: protect.dll (port_proxy cannot start, auto-proxy disabled)
if not exist "..\dist\clash" mkdir "..\dist\clash"
if exist "clash\mihomo.exe" copy /Y "clash\mihomo.exe" "..\dist\clash\mihomo.exe" >nul
if exist "clash\wintun.dll" copy /Y "clash\wintun.dll" "..\dist\clash\wintun.dll" >nul
if exist "protect.dll"      copy /Y "protect.dll"      "..\dist\protect.dll"      >nul

rem so3d_online_accounts.json holds passwords - NOT bundled. Put it next to the exe yourself.
if not exist "..\dist\so3d_online_logs" mkdir "..\dist\so3d_online_logs"

echo [build] done: %CD%\..\dist\so3d_online_login_client.exe
echo [build] bundled: item_names.json / map_id_table.json / map_names.json / monster_names.json / so3d_online_defaults.json
echo [build] bundled: port_proxy.exe + protect.dll (auto-proxy) / clash\mihomo.exe + clash\wintun.dll (egress routing)
echo [build] NOTE: account config NOT bundled - put so3d_online_accounts.json next to the exe yourself.
echo [build] NOTE: socks5_pool.txt auto-creates next to the exe on first use; drop your own exits there.
echo [build] NOTE: copy the whole ..\dist folder to any machine - no python required there anymore.
pause
