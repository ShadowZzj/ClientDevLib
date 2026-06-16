"""一次性抓包探针:用给定账号登录,完整保存 1842 game socket 的 TCP 字节流到 bag_capture.bin。
不改主程序,只对 drain_socket 打补丁按调用顺序收集 label=='game' 的 recv 缓冲。
跑 RUN_SECONDS 秒后停止。之后用 _analyze_capture.py 离线按帧重组分析。
"""
import importlib.util
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLIENT = HERE / "so3d_online_login_client.py"
OUT = HERE / "bag_capture.bin"
RUN_SECONDS = 150

ACCOUNT = "gongyu901121"
PASSWORD = "901121"
CHARACTER = "shadowpope"

spec = importlib.util.spec_from_file_location("sologin", CLIENT)
m = importlib.util.module_from_spec(spec)
sys.modules["sologin"] = m
spec.loader.exec_module(m)

game_chunks: list[bytes] = []
_orig_drain = m.drain_socket


def patched_drain(sock, label, *, enabled, max_reads=16):
    chunks = _orig_drain(sock, label, enabled=enabled, max_reads=max_reads)
    if label == "game":
        game_chunks.extend(chunks)
    return chunks


m.drain_socket = patched_drain

parser = m.build_parser()
args = parser.parse_args([
    "--no-gui",
    "--single",
    "--account", ACCOUNT,
    "--password", PASSWORD,
    "--character", CHARACTER,
    "--run-seconds", "0",
    "--timeout", "12",
    "--no-debug-packets",
])
# 单账号 CLI 路径不会经过 account_namespace,缺 farm 字段会让 run() 在上线后崩。
# 这里把 config_defaults 里缺的键补到 args 上,等价于 GUI 路径的注入。
for k, v in m.config_defaults().items():
    if not hasattr(args, k):
        setattr(args, k, v)

controller = m.AccountController(ACCOUNT, CHARACTER)


def worker():
    try:
        m.run_forever(
            args,
            Path(args.log_dir),
            retry_delay=6.0,
            max_retries=6,
            failure_window_seconds=300.0,
            max_failures_per_window=0,
            console=True,
            controller=controller,
        )
    except Exception as exc:
        print(f"[probe] worker exit: {exc!r}")


t = threading.Thread(target=worker, daemon=True)
t.start()

print(f"[probe] waiting for online, hard cap {RUN_SECONDS}s ...")
CAPTURE_AFTER_ONLINE = 45
deadline = time.time() + RUN_SECONDS
t_online = None
while time.time() < deadline:
    if controller.farm is not None and t_online is None:
        t_online = time.time()
        print(f"[probe] online detected; capturing {CAPTURE_AFTER_ONLINE}s of steady state")
    if t_online is not None and time.time() - t_online >= CAPTURE_AFTER_ONLINE:
        break
    time.sleep(1)
controller.stop_event.set()
for s in controller.take_sockets():
    try:
        s.shutdown(2)
    except OSError:
        pass
    try:
        s.close()
    except OSError:
        pass
t.join(timeout=10)

blob = b"".join(game_chunks)
OUT.write_bytes(blob)
print(f"[probe] saved {len(blob)} bytes ({len(game_chunks)} chunks) -> {OUT}")
