"""实盘验证 GUI「立即买鱼饵 / 立即卖货」两个手动按钮。
按钮本质是往 controller.action_queue 投递 "buy"/"sell",由 worker 线程主循环消费
(so3d_online_login_client.py:2377)并调用 autobuy_run / autosell_run。本脚本走同一条
命令队列路径,等同按钮按下。

为隔离手动路径,关闭自动钓鱼/卖/买的 tick(那几个 tick 受 enabled gate,命令消费不受);
账号 gongyu901121 的真实参数(鱼饵 itemId=3982、vendor=2、wireSlot=0、count=300、
卖货区 50-191)从配置文件抄过来注入 args。

流程:上线 -> 等整桶(511591+511324) -> 打印初始背包 -> put("buy") 等回流 ->
打印买后 -> put("sell") 等回流 -> 打印卖后。所有操作不可逆,已获用户授权。"""
import importlib.util
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CLIENT = HERE / "so3d_online_login_client.py"

ACCOUNT = "gongyu901121"
PASSWORD = "901121"
CHARACTER = "shadowpope"
BAIT_ID = 3982

RUN_SECONDS = 240
WAIT_ONLINE = 150          # 上线 + 首次整桶最长等待
WAIT_AFTER_BUY = 14        # 买后等服务端回流
WAIT_AFTER_SELL = 70       # 卖逐格 0.2s,50-191 最多 142 格,留足时间

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

# --- 真实买卖参数(抄自 so3d_online_accounts.json defaults) ---
args.fishing_bait_item_id = BAIT_ID
args.autobuy_item_id = 0            # 回退到 fishing_bait_item_id=3982
args.autobuy_count = 300
args.autobuy_open_delay = 0.3
args.autobuy_buy_delay = 0.15
args.autosell_start_slot = 50
args.autosell_end_slot = 191
args.autosell_keep_item_ids = []
args.autosell_per_packet_delay = 0.2   # 比配置的 0.5 小,减少 worker 阻塞掉线风险
# --- 关闭自动 tick,只测手动命令队列 ---
args.fishing_enabled = False
args.autosell_enabled = False
args.autobuy_enabled = False

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


def bait_count(farm) -> int:
    return sum(cnt for iid, cnt in farm.bag.values() if iid == BAIT_ID)


def summon_count(farm) -> int:
    return sum(cnt for iid, cnt in farm.cash_bag.values() if iid == m.SUMMON_ITEM_ID)


def dump(farm, title: str) -> None:
    print(f"\n===== {title} =====")
    if farm is None:
        print("  farm is None(未上线)")
        return
    print(f"  bag_observed={farm.bag_observed} cash_observed={farm.cash_observed}")
    print(f"  金币: gold_seen={farm.gold_seen} 当前金币={farm.gold:,}")
    print(f"  鱼饵(itemId={BAIT_ID}) 总数={bait_count(farm)} | 召唤卷(itemId={m.SUMMON_ITEM_ID}) 总数={summon_count(farm)}")
    sell_zone = {s: farm.bag[s] for s in farm.bag if 50 <= s <= 191}
    print(f"  主背包占用 {len(farm.bag)} 格,其中卖货区[50-191] 占用 {len(sell_zone)} 格:")
    for s in sorted(sell_zone):
        iid, cnt = sell_zone[s]
        print(f"    slot{s:<3} itemId={iid:<8} count={cnt}")
    print(f"  cash 背包占用 {len(farm.cash_bag)} 格,召唤卷格 = {m.find_summon_slot(farm)}")


t = threading.Thread(target=worker, daemon=True)
t.start()

print(f"[verify] 等待上线 + 整桶(511591/511324),最长 {WAIT_ONLINE}s ...")
deadline = time.time() + WAIT_ONLINE
while time.time() < deadline:
    farm = controller.farm
    if farm is not None and farm.bag_observed and farm.cash_observed:
        break
    time.sleep(0.5)

farm = controller.farm
if farm is None or not (farm.bag_observed and farm.cash_observed):
    print("[verify] 失败:超时未拿到完整整桶背包,放弃测试")
else:
    dump(farm, "初始背包")

    print("\n[verify] >>> 触发「立即买鱼饵」(action_queue.put('buy'))")
    controller.action_queue.put("buy")
    time.sleep(WAIT_AFTER_BUY)
    dump(controller.farm, f"买鱼饵后(等 {WAIT_AFTER_BUY}s)")

    print("\n[verify] >>> 触发「立即卖货」(action_queue.put('sell'))")
    controller.action_queue.put("sell")
    time.sleep(WAIT_AFTER_SELL)
    dump(controller.farm, f"卖货后(等 {WAIT_AFTER_SELL}s)")

    print("\n[verify] 说明:卖货 autosell_run 会本地把卖出格清零;买鱼饵不本地改数,")
    print("[verify] 鱼饵/召唤卷的真实变化需服务端回流整桶。若上面数字未变,请重连后再看背包确认。")

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
