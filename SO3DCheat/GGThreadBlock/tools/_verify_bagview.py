"""实测「查看背包」数据路径:用授权账号登录,上线后读取 controller.farm.bag / cash_bag
(这正是 GUI open_bag_view 弹窗读取的同一份快照),打印验证整桶包已被 update_farm_state 重建。
跑到拿到背包+cash 数据或超时即停。"""
import importlib.util
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLIENT = HERE / "so3d_online_login_client.py"
RUN_SECONDS = 150
CAPTURE_AFTER_ONLINE = 40

ACCOUNT = "gongyu901121"
PASSWORD = "901121"
CHARACTER = "shadowpope"

spec = importlib.util.spec_from_file_location("sologin", CLIENT)
m = importlib.util.module_from_spec(spec)
sys.modules["sologin"] = m
spec.loader.exec_module(m)

parser = m.build_parser()
args = parser.parse_args([
    "--no-gui", "--single",
    "--account", ACCOUNT, "--password", PASSWORD, "--character", CHARACTER,
    "--run-seconds", "0", "--timeout", "12", "--no-debug-packets",
])
for k, v in m.config_defaults().items():
    if not hasattr(args, k):
        setattr(args, k, v)

controller = m.AccountController(ACCOUNT, CHARACTER)


def worker():
    try:
        m.run_forever(
            args, Path(args.log_dir),
            retry_delay=6.0, max_retries=6,
            failure_window_seconds=300.0, max_failures_per_window=0,
            console=True, controller=controller,
        )
    except Exception as exc:
        print(f"[verify] worker exit: {exc!r}")


t = threading.Thread(target=worker, daemon=True)
t.start()

print(f"[verify] waiting for online + bag bulk, hard cap {RUN_SECONDS}s ...")
deadline = time.time() + RUN_SECONDS
t_online = None
got = False
while time.time() < deadline:
    farm = controller.farm
    if farm is not None and t_online is None:
        t_online = time.time()
        print("[verify] online detected; waiting for enter-burst inventory packets")
    if farm is not None and farm.bag_observed and farm.cash_observed:
        got = True
        break
    if t_online is not None and time.time() - t_online >= CAPTURE_AFTER_ONLINE:
        break
    time.sleep(0.5)

farm = controller.farm
print(f"\n===== bag view snapshot (got_full={got}) =====")
if farm is None:
    print("[verify] FAILED: never went online (controller.farm is None)")
else:
    bag = dict(farm.bag)
    cash = dict(farm.cash_bag)
    print(f"bag_observed={farm.bag_observed} 主背包占用 {len(bag)} 格")
    for s in sorted(bag):
        iid, cnt = bag[s]
        print(f"  slot{s:<3} itemId={iid:<8} count={cnt}")
    print(f"cash_observed={farm.cash_observed} cash背包占用 {len(cash)} 格")
    for s in sorted(cash):
        iid, cnt = cash[s]
        print(f"  slot{s:<3} itemId={iid:<8} count={cnt}")
    print(f"召唤卷(8036)所在 cash 格 = {m.find_summon_slot(farm)}")

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
print("[verify] done")
