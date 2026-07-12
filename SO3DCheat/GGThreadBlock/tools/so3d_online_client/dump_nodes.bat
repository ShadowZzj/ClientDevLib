@echo off
cd /d "%~dp0"

REM Print the candidate node ip table only (no pin, no rebind). Use it to pick a
REM STABLE node ip for start_proxies.bat. Table refreshes each round; a line
REM   [node] candidates(...)
REM prints whenever the set changes. Pick ips that show up every round.
REM NOTE: this dump uses the default dataDir, so don't run two dumps at once
REM (single-instance lock -> 1009). Running it alongside pinned instances is
REM fine (they each use their own GGTB_<node-ip> dataDir).
set "PYTHON=python"
set "GGTB_NODE_DEBUG=1"
%PYTHON% port_proxy.py
pause
