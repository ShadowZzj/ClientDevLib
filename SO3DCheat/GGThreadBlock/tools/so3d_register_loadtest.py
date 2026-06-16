#!/usr/bin/env python3
"""SO3D register-endpoint batch/load tester.

Hammers the local test server's POST /api/register to gauge how much
registration load it sustains. Usernames/passwords are built from common
English words + digits so a batch does not obviously read as one account;
accounts are grouped three at a time (xxxx1/xxxx2/xxxx3) sharing one
password and one randomly generated email. Every successful registration
is appended to a maintained JSON file so the batch is restartable.

Edit CONFIG below, then run:
    python SO3DCheat\\GGThreadBlock\\tools\\so3d_register_loadtest.py
"""

from __future__ import annotations

import argparse
import json
import random
import string
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path

import requests
from requests.adapters import HTTPAdapter
from urllib3.exceptions import InsecureRequestWarning
from urllib3.util.retry import Retry

requests.packages.urllib3.disable_warnings(InsecureRequestWarning)  # self-signed test cert


CONFIG = {
    "base_url": "https://127.0.0.1",
    "register_path": "/api/register",
    "origin": "https://127.0.0.1",
    "referer": "https://shop2.guguseal.com/",
    "groups": 50,          # number of 3-account groups to register
    "group_size": 3,       # accounts per shared password/email group
    "workers": 10,         # concurrent requests
    "timeout": 15.0,       # per-request seconds
    "verify_tls": False,   # local test server uses a self-signed cert
    "output_file": str(Path(__file__).with_name("so3d_registered_accounts.json")),
    "save_every": 1,       # flush the JSON store after this many new successes
    "seed": 0,             # 0 = nondeterministic; set for reproducible batches
}

SUCCESS_TOKEN = "成功"  # appears in the 註冊成功！ message body

# Common, unremarkable English words. Username = word + word + digits, so a
# batch looks like a crowd of unrelated players rather than one enumerated run.
WORDS = [
    "apple", "river", "stone", "tiger", "cloud", "maple", "amber", "ocean",
    "frost", "ember", "noble", "quartz", "willow", "cedar", "raven", "lunar",
    "solar", "pearl", "coral", "flint", "birch", "olive", "hazel", "ivory",
    "comet", "delta", "echo", "ferro", "glade", "harbor", "indigo", "jasper",
    "kelp", "lotus", "mango", "nectar", "opal", "piper", "quill", "rusty",
    "sable", "topaz", "umber", "violet", "walnut", "xenon", "yarrow", "zephyr",
    "brave", "candy", "drift", "eagle", "fable", "grove", "honey", "icarus",
    "jolly", "koala", "lemon", "misty", "north", "onyx", "prism", "quick",
]

EMAIL_DOMAINS = ["qq.com", "163.com", "gmail.com", "outlook.com", "126.com", "foxmail.com"]

_PRINT_LOCK = threading.Lock()


def _print(*values: object) -> None:
    stamp = time.strftime("%H:%M:%S", time.localtime())
    with _PRINT_LOCK:
        print(f"{stamp} |", *values, flush=True)


@dataclass
class GroupPlan:
    index: int
    usernames: list[str]
    password: str
    email: str


@dataclass
class Results:
    lock: threading.Lock = field(default_factory=threading.Lock)
    ok: int = 0
    fail: int = 0
    latencies: list[float] = field(default_factory=list)


def make_password(rng: random.Random) -> str:
    word = rng.choice(WORDS).capitalize()
    digits = "".join(rng.choice(string.digits) for _ in range(rng.randint(3, 4)))
    return f"{word}{digits}"


def make_email(rng: random.Random) -> str:
    word = rng.choice(WORDS)
    digits = "".join(rng.choice(string.digits) for _ in range(rng.randint(3, 6)))
    return f"{word}{digits}@{rng.choice(EMAIL_DOMAINS)}"


def make_group(index: int, group_size: int, rng: random.Random, used: set[str]) -> GroupPlan:
    while True:
        stem = rng.choice(WORDS) + rng.choice(WORDS)
        if stem not in used:
            used.add(stem)
            break
    base_num = rng.randint(1, 9)
    usernames = [f"{stem}{base_num + offset}" for offset in range(group_size)]
    return GroupPlan(
        index=index,
        usernames=usernames,
        password=make_password(rng),
        email=make_email(rng),
    )


class AccountStore:
    """Maintained JSON file of successfully registered accounts."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self._lock = threading.Lock()
        self._pending = 0
        self.records: list[dict[str, object]] = []
        self.known: set[str] = set()
        if path.exists():
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                self.records = list(data.get("accounts", []))
                self.known = {str(rec.get("username", "")) for rec in self.records}
                _print(f"[store] loaded {len(self.records)} existing account(s) from {path.name}")
            except (json.JSONDecodeError, OSError) as exc:
                _print(f"[store] could not read {path.name} ({exc}); starting fresh")

    def has(self, username: str) -> bool:
        with self._lock:
            return username in self.known

    def add(self, record: dict[str, object], save_every: int) -> None:
        with self._lock:
            if record["username"] in self.known:
                return
            self.records.append(record)
            self.known.add(str(record["username"]))
            self._pending += 1
            if self._pending >= max(1, save_every):
                self._flush_locked()

    def flush(self) -> None:
        with self._lock:
            self._flush_locked()

    def _flush_locked(self) -> None:
        payload = {
            "updated": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()),
            "count": len(self.records),
            "accounts": self.records,
        }
        tmp = self.path.with_suffix(self.path.suffix + ".tmp")
        tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
        tmp.replace(self.path)
        self._pending = 0


def build_session(verify_tls: bool) -> requests.Session:
    session = requests.Session()
    retry = Retry(total=2, backoff_factor=0.3, status_forcelist=(502, 503, 504))
    adapter = HTTPAdapter(max_retries=retry, pool_connections=64, pool_maxsize=64)
    session.mount("https://", adapter)
    session.mount("http://", adapter)
    session.verify = verify_tls
    return session


def request_headers(cfg: dict[str, object]) -> dict[str, str]:
    return {
        "accept": "*/*",
        "accept-language": "zh-CN,zh;q=0.9",
        "cache-control": "no-cache",
        "content-type": "application/json",
        "origin": str(cfg["origin"]),
        "pragma": "no-cache",
        "referer": str(cfg["referer"]),
        "user-agent": (
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/148.0.0.0 Safari/537.36"
        ),
    }


def register_one(
    session: requests.Session,
    url: str,
    headers: dict[str, str],
    username: str,
    password: str,
    email: str,
    timeout: float,
) -> tuple[bool, str, float]:
    payload = {
        "username": username,
        "password": password,
        "confirmPassword": password,
        "email": email,
    }
    start = time.monotonic()
    try:
        resp = session.post(url, json=payload, headers=headers, timeout=timeout)
    except requests.RequestException as exc:
        return False, f"error:{exc.__class__.__name__}", time.monotonic() - start
    elapsed = time.monotonic() - start
    text = resp.text or ""
    ok = resp.status_code == 200 and SUCCESS_TOKEN in text
    detail = text.strip()[:120].replace("\n", " ")
    return ok, f"{resp.status_code}:{detail}", elapsed


def run_group(
    group: GroupPlan,
    session: requests.Session,
    url: str,
    headers: dict[str, str],
    store: AccountStore,
    results: Results,
    cfg: dict[str, object],
) -> None:
    timeout = float(cfg["timeout"])
    save_every = int(cfg["save_every"])
    for username in group.usernames:
        if store.has(username):
            _print(f"[skip] {username} already registered")
            continue
        ok, detail, elapsed = register_one(
            session, url, headers, username, group.password, group.email, timeout
        )
        with results.lock:
            results.latencies.append(elapsed)
            if ok:
                results.ok += 1
            else:
                results.fail += 1
        if ok:
            store.add(
                {
                    "username": username,
                    "password": group.password,
                    "email": group.email,
                    "group": group.index,
                    "registered_at": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime()),
                },
                save_every,
            )
            _print(f"[ok] g{group.index:03d} {username} ({elapsed * 1000:.0f}ms)")
        else:
            _print(f"[fail] g{group.index:03d} {username} -> {detail} ({elapsed * 1000:.0f}ms)")


def summarize(results: Results, wall: float) -> None:
    total = results.ok + results.fail
    lat = sorted(results.latencies)
    _print("=" * 60)
    _print(f"[summary] total={total} ok={results.ok} fail={results.fail} wall={wall:.2f}s")
    if total and wall > 0:
        _print(f"[summary] throughput={total / wall:.1f} req/s, success={results.ok / wall:.1f} acct/s")
    if lat:
        def pct(p: float) -> float:
            return lat[min(len(lat) - 1, int(len(lat) * p))]
        _print(
            f"[summary] latency avg={sum(lat) / len(lat) * 1000:.0f}ms "
            f"p50={pct(0.50) * 1000:.0f}ms p95={pct(0.95) * 1000:.0f}ms "
            f"max={lat[-1] * 1000:.0f}ms"
        )
    _print("=" * 60)


def main() -> None:
    parser = argparse.ArgumentParser(description="SO3D register-endpoint batch/load tester")
    parser.add_argument("--base-url", default=CONFIG["base_url"])
    parser.add_argument("--groups", type=int, default=CONFIG["groups"], help="number of 3-account groups")
    parser.add_argument("--group-size", type=int, default=CONFIG["group_size"])
    parser.add_argument("--workers", type=int, default=CONFIG["workers"], help="concurrent requests")
    parser.add_argument("--timeout", type=float, default=CONFIG["timeout"])
    parser.add_argument("--output-file", default=CONFIG["output_file"])
    parser.add_argument("--seed", type=int, default=CONFIG["seed"], help="0 = nondeterministic")
    parser.add_argument("--verify-tls", action=argparse.BooleanOptionalAction, default=CONFIG["verify_tls"])
    args = parser.parse_args()

    rng = random.Random(args.seed or None)
    url = args.base_url.rstrip("/") + str(CONFIG["register_path"])
    headers = request_headers({**CONFIG, "origin": CONFIG["origin"], "referer": CONFIG["referer"]})
    store = AccountStore(Path(args.output_file))
    results = Results()
    session = build_session(args.verify_tls)

    used_stems: set[str] = set()
    plans = [make_group(i + 1, args.group_size, rng, used_stems) for i in range(args.groups)]
    total_accounts = sum(len(p.usernames) for p in plans)
    _print(
        f"[start] {args.groups} group(s) x {args.group_size} = {total_accounts} account(s), "
        f"workers={args.workers}, url={url}"
    )

    cfg = {"timeout": args.timeout, "save_every": int(CONFIG["save_every"])}
    wall_start = time.monotonic()
    try:
        with ThreadPoolExecutor(max_workers=max(1, args.workers)) as pool:
            futures = [
                pool.submit(run_group, plan, session, url, headers, store, results, cfg)
                for plan in plans
            ]
            for future in as_completed(futures):
                future.result()
    except KeyboardInterrupt:
        _print("[stop] Ctrl+C; flushing store")
    finally:
        store.flush()
        summarize(results, time.monotonic() - wall_start)
        _print(f"[store] saved {len(store.records)} account(s) to {store.path}")


if __name__ == "__main__":
    main()


