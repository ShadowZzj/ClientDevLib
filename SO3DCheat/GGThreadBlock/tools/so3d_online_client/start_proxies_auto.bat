@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ===== Automatic load-balancing launcher (no manual node pinning) =====
REM Opens COUNT windows. Each is one port_proxy.py process with GGTB_AUTO=1,
REM so it auto-claims the lowest-latency upstream node that no other live
REM instance has taken yet (coordinated through a shared registry file in
REM %TEMP%\ggtb_auto_route\registry.json). No GGTB_NODE_IP here -> nodes are
REM picked automatically by measured RTT.
REM
REM Each instance listens on a distinct loopback alias 127.2.57.N (N=1..COUNT).
REM In the bot accounts set:
REM     "proxy_host"        : "auto"   (let the bot pick a live instance by RTT)
REM     "proxy_max_per_node": <cap>    (max accounts per node; 0 = unlimited)
REM
REM Watch each window for:
REM   [node] AUTO-CLAIMED -> <ip> (rtt=..)  (this instance claimed a node)
REM   [node] PINNED -> ...                  (locked to that node afterwards)
REM   [bind] ... ret=0                     (listen alias moved, no port clash)
REM Close a window to stop that instance (its slot frees on pid death).

REM ---- How many proxy instances to launch ----
set "COUNT=5"

REM Python interpreter (on PATH = python; else full path, no spaces):
set "PYTHON=python"

for /L %%N in (1,1,%COUNT%) do call :launch %%N

echo.
echo [done] Launched %COUNT% auto instances, one window each.
echo        Set "proxy_host":"auto" + "proxy_max_per_node":N per bot account.
pause
exit /b 0

:launch
set "N=%~1"
set "GGTB_NODE_IP="
set "GGTB_AUTO=1"
set "GGTB_LISTEN_IP=127.2.57.%N%"
echo [launch] N=%N%  auto=1  listen=127.2.57.%N% / 127.4.57.%N%
start "proxy-auto-%N% listen=127.2.57.%N%" cmd /k "%PYTHON% port_proxy.py"
exit /b 0
