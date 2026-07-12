import json
import os
import random
import sys
import threading
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# 注册成功后用 so3dclient 包(同目录 so3d_online_client/)登入测试服并建号
_TOOLS_DIR = Path(__file__).resolve().parent
_PKG_DIR = _TOOLS_DIR / "so3d_online_client"
if str(_PKG_DIR) not in sys.path:
    sys.path.insert(0, str(_PKG_DIR))

# ======== 在这里改参数 ========
ACCOUNT = "shouliga"   # 账号前缀
PASSWORD = "qweqwe"   # 密码
COUNT = 10             # 后缀注册到几 (会注册 ACCOUNT1 ... ACCOUNTn)
CHARACTER = "shouliga"         # 角色名前缀 (建 CHARACTER1 ... CHARACTERn);留空则用账号名
THREADS = 4            # 并发处理几个账号 (1 = 串行)
RETRY = 3             # 建号失败重试次数
RETRY_DELAY = 3.0     # 每次重试前等待 (秒)
# =============================

URL = "https://shop2.guguseal.com/api/register"
COOKIE = "PHPSESSID=cjdukistq2at0kov4brb9vtjel"

# 建号成功后把账号挂到 broker(跑店)账号池
BROKER_URL = "http://127.0.0.1:7321/api/paodian/accounts"

# 建号成功后,把账号名以 "#账号" 形式逐行追加到这里(带 # 默认不参与登录,需要时手动去掉 #)
AUTOLOGIN_FILE = r"G:\GuGuSealKarel1120\autologin.txt"

# 注册成功后，以此文件为模板，在 CONFIG_BASE/<账号>/ 下生成 ngpz.txt
CONFIG_BASE = r"G:\GuGuSealKarel1120\Config"
TEMPLATE = os.path.join(CONFIG_BASE, "cxksren2", "ngpz.txt")
ACCOUNT_SECTION = "[登入账号_B]"
PASSWORD_SECTION = "[登入密码_B]"

HEADERS = {
    "accept": "*/*",
    "accept-language": "zh-CN,zh;q=0.9",
    "cache-control": "no-cache",
    "content-type": "application/json",
    "cookie": COOKIE,
    "origin": "https://shop2.guguseal.com",
    "pragma": "no-cache",
    "referer": "https://shop2.guguseal.com/",
    "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                  "(KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36",
}


def register(username, password):
    email = f"{random.randint(100000, 999999)}@qq.com"
    payload = json.dumps({
        "username": username,
        "password": password,
        "confirmPassword": password,
        "email": email,
    }).encode("utf-8")

    req = urllib.request.Request(URL, data=payload, headers=HEADERS, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            body = resp.read().decode("utf-8", "replace")
            return resp.status, body
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:
        return None, str(e)


def write_account_config(username, password):
    # 模板是 GBK 编码 + CRLF 行尾，原样保留；只替换账号/密码两段下面的 bjk_nr= 值
    text = open(TEMPLATE, "r", encoding="gbk", newline="").read()
    lines = text.split("\n")
    pending = None
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == ACCOUNT_SECTION:
            pending = username
        elif stripped == PASSWORD_SECTION:
            pending = password
        elif pending is not None and stripped.startswith("bjk_nr="):
            eol = "\r" if line.endswith("\r") else ""
            lines[i] = f"bjk_nr={pending}{eol}"
            pending = None

    dest_dir = os.path.join(CONFIG_BASE, username)
    os.makedirs(dest_dir, exist_ok=True)
    dest = os.path.join(dest_dir, "ngpz.txt")
    with open(dest, "w", encoding="gbk", newline="") as f:
        f.write("\n".join(lines))
    return dest


def create_character(username, password, character):
    # 测试服直连登入并建号,全部走 so3dclient 包的协议原语
    from so3dclient.config import namespace_from_config, account_namespace
    from so3dclient.session import run_create_character
    base = namespace_from_config({})
    args = account_namespace(base, {}, {
        "account": username,
        "password": password,
        "character": character,
    })
    return run_create_character(args)


def add_account_to_login_client(username, password, character):
    """建号成功后,把账号写进登录器的 accounts.json(默认 enabled=False 不自动登录,
    稍后在 GUI 里手动启动)。走 so3dclient.config 里带文件锁的接口,多线程/多进程都安全。"""
    try:
        from so3dclient.config import append_accounts_to_file
        from so3dclient.runtime import CONFIG
    except Exception as exc:
        print(f"[{username}] 无法导入登录器接口，跳过写账号: {exc}")
        return
    accounts_file = CONFIG["accounts_file"]
    try:
        added = append_accounts_to_file(accounts_file, [{
            "account": username,
            "password": password,
            "character": character,
        }])
    except Exception as exc:
        print(f"[{username}] 写入登录器账号文件失败: {exc}")
        return
    if added:
        print(f"[{username}] 已加入登录器账号文件 {accounts_file}（默认不登录）")
    else:
        print(f"[{username}] 已存在于登录器账号文件，跳过")


_AUTOLOGIN_LOCK = threading.Lock()


def append_to_autologin(username):
    """把账号名以 '#账号' 形式追加进 autologin.txt(每行一个,默认带 # 注释掉=不自动登录)。
    已存在(带或不带 #)就跳过;文件末尾没换行先补一个,避免和上一行粘成一行。
    THREADS>1 时多线程并发建号,用锁串行化这段读改写。"""
    name = str(username).strip()
    if not name:
        return
    with _AUTOLOGIN_LOCK:
        try:
            existing = ""
            if os.path.exists(AUTOLOGIN_FILE):
                with open(AUTOLOGIN_FILE, "r", encoding="utf-8", errors="replace") as f:
                    existing = f.read()
            present = {ln.strip().lstrip("#").strip() for ln in existing.splitlines()}
            if name in present:
                print(f"[{name}] autologin.txt 已存在，跳过")
                return
            prefix = "" if (existing == "" or existing.endswith("\n")) else "\n"
            with open(AUTOLOGIN_FILE, "a", encoding="utf-8") as f:
                f.write(f"{prefix}#{name}\n")
            print(f"[{name}] 已追加到 autologin.txt（#{name}）")
        except Exception as exc:
            print(f"[{name}] 写 autologin.txt 失败: {exc}")


def add_to_broker(username, password):
    payload = json.dumps({"username": username, "password": password}).encode("utf-8")
    req = urllib.request.Request(
        BROKER_URL, data=payload,
        headers={"Content-Type": "application/json"}, method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return resp.status, resp.read().decode("utf-8", "replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "replace")
    except Exception as e:
        return None, str(e)


def create_character_with_retry(username, password, character):
    # run_create_character 幂等(角色已存在会跳过),重试到成功或用尽次数
    for attempt in range(1, RETRY + 1):
        try:
            if create_character(username, password, character):
                print(f"[{username}] create_character {character!r} -> OK (attempt {attempt})")
                return True
            print(f"[{username}] create_character {character!r} attempt {attempt}/{RETRY} -> FAILED")
        except Exception as exc:
            print(f"[{username}] create_character {character!r} attempt {attempt}/{RETRY} error: {exc}")
        if attempt < RETRY:
            time.sleep(RETRY_DELAY)
    print(f"[{username}] create_character {character!r} -> GIVE UP after {RETRY} attempts")
    return False


def register_account(i):
    """阶段1(可并行):注册 + 写角色配置 + 建号。成功返回 (username, password, character),
    任一步失败返回 None。这一段只碰各账号自己的资源(网络/各自的 ngpz.txt),并行无副作用。"""
    username = f"{ACCOUNT}{i}"
    character = f"{CHARACTER}{i}" if CHARACTER else username
    status, body = register(username, PASSWORD)
    print(f"[{username}] status={status} -> {body}")
    if status != 200:
        return None
    dest = write_account_config(username, PASSWORD)
    print(f"[{username}] config -> {dest}")
    if not create_character_with_retry(username, PASSWORD, character):
        return None
    return (username, PASSWORD, character)


def commit_account(username, password, character):
    """阶段2(按序号 1..N 串行):把账号写进登录器 accounts.json + autologin.txt,再挂到 broker。
    串行执行是为了让这两个共享文件按账号序号顺序写入(1 在前、N 在后),不被线程完成次序打乱。"""
    add_account_to_login_client(username, password, character)
    append_to_autologin(username)
    status, body = add_to_broker(username, password)
    print(f"[{username}] broker -> status={status} {body}")


def main():
    indices = range(1, COUNT + 1)
    if THREADS <= 1:
        for i in indices:
            result = register_account(i)
            if result:
                commit_account(*result)
        return
    # 多线程:注册/建号并行,结果按序号收集;两个共享文件的写入在最后按 1..N 顺序提交。
    results: dict[int, tuple] = {}
    with ThreadPoolExecutor(max_workers=THREADS) as pool:
        futures = {pool.submit(register_account, i): i for i in indices}
        for fut in as_completed(futures):
            i = futures[fut]
            try:
                results[i] = fut.result()
            except Exception as exc:
                print(f"[{ACCOUNT}{i}] worker crashed: {exc}")
                results[i] = None
    for i in indices:
        result = results.get(i)
        if result:
            commit_account(*result)


if __name__ == "__main__":
    main()
