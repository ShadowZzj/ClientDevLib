@echo off
setlocal
cd /d "%~dp0"

REM ===== Multi-instance load-balancing launcher =====
REM Each instance = one port_proxy.py process pinned to a different upstream
REM node and listening on a different loopback alias, so bots don't all pile
REM onto a single edge node. Each instance opens its own window; watch for
REM   [node] PINNED -> ...   (pinned to the target node)
REM   [bind] ... ret=0       (listen alias moved, no port clash)
REM Close a window to stop that instance.
REM
REM Python interpreter (on PATH = python; else full path, no spaces):
set "PYTHON=python"

REM ---- Instance list: call :launch <N> <node-ip> ----
REM   N       : instance number -> listen aliases 127.2.57.N and 127.4.57.N.
REM             In the bot account set "proxy_host" to the matching 127.2.57.N.
REM   node-ip : upstream node to pin. Use dump_nodes.bat to list candidates and
REM             pick a STABLE ip (low-latency nodes rotate ip and cause PIN MISS).
REM   dataDir is isolated per node-ip (APPDATA\GGTB_<node-ip>\Protect), so do
REM   NOT reuse the same node-ip for two instances. Edit the lines below:
call :launch 26 103.73.162.254
call :launch 27 202.189.11.31

echo.
echo [done] All instances launched, one window each.
echo        Set "proxy_host":"127.2.57.N" per bot account to split traffic.
pause
exit /b 0

:launch
set "N=%~1"
set "NODE=%~2"
set "GGTB_NODE_IP=%NODE%"
set "GGTB_LISTEN_IP=127.2.57.%N%"
echo [launch] N=%N%  node=%NODE%  listen=127.2.57.%N% / 127.4.57.%N%
start "proxy-%N% node=%NODE%" cmd /k "%PYTHON% port_proxy.py"
exit /b 0
