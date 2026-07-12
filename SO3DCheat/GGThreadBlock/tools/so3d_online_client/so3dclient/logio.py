"""日志原语:per-account 流路由、UI/console 分发、轮转文件日志。"""
from __future__ import annotations

import argparse
import builtins
import re
import threading
import time
from pathlib import Path

from .runtime import CONFIG

_PRINT_LOCK = threading.Lock()
_LOG_CONTEXT = threading.local()
_BUILTIN_PRINT = builtins.print
_LOG_HANDLER = None
_STATUS_HANDLER = None


def set_log_handler(handler) -> None:
    global _LOG_HANDLER
    _LOG_HANDLER = handler


def set_status_handler(handler) -> None:
    global _STATUS_HANDLER
    _STATUS_HANDLER = handler


def report_account_status(account: str, character: str, status: str) -> None:
    handler = _STATUS_HANDLER
    if handler is None:
        return
    try:
        handler(account, character, status)
    except Exception:
        pass


def _emit_line(line: str, *, console: bool) -> None:
    with _PRINT_LOCK:
        if console:
            try:
                _BUILTIN_PRINT(line, flush=True)
            except Exception:
                pass
        handler = _LOG_HANDLER
        if handler is not None:
            try:
                handler(line)
            except Exception:
                pass


def _timestamp() -> str:
    now = time.time()
    stamp = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now))
    return f"{stamp}.{int((now % 1) * 1000):03d}"


def _log_print(*values: object, sep: str = " ", end: str = "\n", file=None, flush: bool = False) -> None:
    if file is not None:
        _BUILTIN_PRINT(*values, sep=sep, end=end, file=file, flush=flush)
        return

    text = sep.join(str(value) for value in values)
    line = f"{_timestamp()} | {text}"
    stream = getattr(_LOG_CONTEXT, "stream", None)
    console = bool(getattr(_LOG_CONTEXT, "console", True))
    with _PRINT_LOCK:
        if stream is not None:
            stream.write(line + end)
            stream.flush()
    if console:
        _emit_line(line, console=True)


def biz_log(message: str, *, ui: bool = False) -> None:
    """写一行干净的业务事件到独立的 <account>_<character>.biz.log。
    只进 biz 流,不进主 .log、不进 console —— 主 .log 保留全部噪音不变。
    biz_stream 未设置时(self-test / 未上线)静默跳过。

    ui=True 时额外把这行推给 GUI 日志面板(带 [账号/角色] 标签,多账号可区分);
    console 始终不刷,所以 CLI 行为不变、只有 GUI 设了 handler 才显示。
    供「买->银行搬到背包->摆摊->卖出」这类整体流程事件用;钓鱼/收线等高频事件不传 ui,避免刷屏。"""
    stream = getattr(_LOG_CONTEXT, "biz_stream", None)
    if stream is None:
        return
    ts = _timestamp()
    with _PRINT_LOCK:
        try:
            stream.write(f"{ts} | {message}\n")
            stream.flush()
        except Exception:
            pass
    if ui:
        label = getattr(_LOG_CONTEXT, "biz_label", "")
        _emit_line(f"{ts} | [{label}] {message}" if label else f"{ts} | {message}",
                   console=False)


def main_log(message: str) -> None:
    _emit_line(f"{_timestamp()} | {message}", console=True)


def event_log(message: str) -> None:
    """重要事件:写当前账号 .log 文件 + 始终推 UI/console。
    GUI 下账号会话 console=False,print 只进文件不进 UI;这里把文件流和 UI 各发一次,
    既保留文件记录又让界面看得到(不走 console 标志,因此 CLI 也不会重复)。"""
    line = f"{_timestamp()} | {message}"
    stream = getattr(_LOG_CONTEXT, "stream", None)
    with _PRINT_LOCK:
        if stream is not None:
            stream.write(line + "\n")
            stream.flush()
    _emit_line(line, console=True)


# GUI 日志上色用的端口集合。测试服/正式服(本地代理)端口对照:
#   login 1818(test) / 10002(prod) —— 与颜色无关,仅备查;
#   bill  1838(两服一致);chat 3000(两服一致);game 1841/1842(两服一致)。
# 因此 game 主端口与 chat 端口在两种 server_mode 下数值相同,无需按模式切换。
LOG_LOGIN_PORTS = (1818, 10002)
LOG_BILL_PORTS = (1838,)
LOG_CHAT_PORTS = (3000,)
LOG_GAME_PORTS = (1841, 1842)

# 行内端口提取:只认被 ( [ : 包起来的 3-5 位数字(避开时间戳 00:49:13 那种 2 位段)。
_LOG_PORT_RE = re.compile(r"[(\[:](\d{3,5})[)\]\s,]")
# 主端口被动断链关键字(reset/管线破裂/连接被关)。
_LOG_RESET_KEYWORDS = ("connectionreset", "10054", "10053", "强迫关闭", "重置", "broken pipe", "断链")
# 断链/失败动作词,配合端口判定 game 主端口异常。
_LOG_DISCONNECT_KEYWORDS = ("断开", "断链", "重置", "失败", "reset", "closed")


def _ports_in_line(line: str) -> set[int]:
    out: set[int] = set()
    for match in _LOG_PORT_RE.finditer(line):
        try:
            out.add(int(match.group(1)))
        except ValueError:
            pass
    return out


def classify_log_line(line: str) -> str | None:
    """把一行 GUI 日志归类成颜色标签:
      "ok"   绿  —— 登录成功/已上线
      "warn" 黄  —— chat(3000) 独立会话断开/重连;secondary 登录类失败
      "err"  红  —— game 主端口(1841/1842)被 reset 断链;僵尸进场
    无法归类返回 None(GUI 用默认前景色)。判定有先后:chat 优先于通用 reset,
    这样「chat(3000)…reset…」只标黄(独立会话,不拖垮整局),不会被误判成主端口红。"""
    low = line.lower()
    ports = _ports_in_line(line)
    chat_hit = "chat(" in low or bool(ports & set(LOG_CHAT_PORTS))
    if chat_hit and ("chat(" in low or any(k in line for k in _LOG_DISCONNECT_KEYWORDS) or "无数据" in line):
        return "warn"
    if "僵尸进场" in line:
        return "err"
    if any(k in low for k in _LOG_RESET_KEYWORDS):
        return "err"
    if (ports & set(LOG_GAME_PORTS)) and any(k in line for k in _LOG_DISCONNECT_KEYWORDS):
        return "err"
    if "secondary" in low:
        return "warn"
    if "登录成功" in line or "已上线" in line:
        return "ok"
    return None


def sanitize_log_name(value: object) -> str:
    text = str(value or "").strip()
    text = re.sub(r"[^0-9A-Za-z._-]+", "_", text)
    text = text.strip("._-")
    return text or "account"


def account_log_path(log_dir: Path, args: argparse.Namespace) -> Path:
    name = sanitize_log_name(f"{args.account}_{args.character}")
    return log_dir / f"{name}.log"


def account_biz_log_path(log_dir: Path, args: argparse.Namespace) -> Path:
    name = sanitize_log_name(f"{args.account}_{args.character}")
    return log_dir / f"{name}.biz.log"


class RotatingTextLog:
    def __init__(self, path: Path, *, max_bytes: int, max_files: int) -> None:
        self.path = path
        self.max_bytes = max(1024, int(max_bytes))
        self.max_files = max(1, int(max_files))
        self._stream = None
        self._size = 0

    def __enter__(self) -> "RotatingTextLog":
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._open()
        if self._size >= self.max_bytes:
            self._rotate()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def write(self, text: str) -> int:
        if not isinstance(text, str):
            text = str(text)
        encoded_size = len(text.encode("utf-8", errors="replace"))
        if self._size > 0 and self._size + encoded_size > self.max_bytes:
            self._rotate()
        assert self._stream is not None
        written = self._stream.write(text)
        self._size += encoded_size
        return written

    def flush(self) -> None:
        if self._stream is not None:
            self._stream.flush()

    def close(self) -> None:
        if self._stream is not None:
            self._stream.close()
            self._stream = None

    def _open(self) -> None:
        self._size = self.path.stat().st_size if self.path.exists() else 0
        self._stream = self.path.open("a", encoding="utf-8", buffering=1)

    def _backup_path(self, index: int) -> Path:
        return self.path.with_name(f"{self.path.name}.{index}")

    def _rotate(self) -> None:
        self.close()
        if self.max_files <= 1:
            self.path.unlink(missing_ok=True)
            self._open()
            return

        last = self.max_files - 1
        self._backup_path(last).unlink(missing_ok=True)
        for index in range(last - 1, 0, -1):
            src = self._backup_path(index)
            if src.exists():
                src.replace(self._backup_path(index + 1))
        if self.path.exists():
            self.path.replace(self._backup_path(1))
        self._open()


def log_max_bytes(args: argparse.Namespace) -> int:
    max_mb = float(getattr(args, "log_max_mb", CONFIG["log_max_mb"]))
    return int(max_mb * 1024 * 1024)


def log_max_files(args: argparse.Namespace) -> int:
    return int(getattr(args, "log_max_files", CONFIG["log_max_files"]))


def open_account_log(log_dir: Path, args: argparse.Namespace) -> RotatingTextLog:
    return RotatingTextLog(
        account_log_path(log_dir, args),
        max_bytes=log_max_bytes(args),
        max_files=log_max_files(args),
    )


def open_account_biz_log(log_dir: Path, args: argparse.Namespace) -> RotatingTextLog:
    return RotatingTextLog(
        account_biz_log_path(log_dir, args),
        max_bytes=log_max_bytes(args),
        max_files=log_max_files(args),
    )


log_print = _log_print
