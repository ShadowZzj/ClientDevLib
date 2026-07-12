"""Tkinter 批量登录器界面。"""
from __future__ import annotations

import argparse
import os
import queue
import socket
import threading
import time
import traceback
from pathlib import Path

# 正式服(prod)需要的 123.dll layer-1 解密桥;测试服不依赖,缺失不影响。
try:
    from . import bridge_client
except Exception:  # pragma: no cover - bridge optional for test mode
    bridge_client = None

from .runtime import AccountController, CONFIG, LOGIN_GATE, PROD_PROXY_HOST
from .logio import classify_log_line, main_log, set_log_handler, set_status_handler
from . import socks5_pool
from . import auto_route
from .protocol import (
    BAG_MAIN_SLOTS,
    FARM_FIELD_SPECS,
    FISHING_OFF_MARK,
    FISHING_ON_MARK,
    REVIVE_TOWNS,
    STALL_BAG_CASH,
    STALL_BAG_NORMAL,
    STALL_MAX_SLOTS,
    SUMMON_ITEM_ID,
    _coerce_int_list,
    parse_int_config,
)
from .tables import (
    item_label,
    item_name,
    monster_name,
    resolve_item_id,
    resolve_item_id_list,
    resolve_map_name,
)
from .config import (
    _as_bool,
    account_namespace,
    load_accounts_document,
    save_accounts_document,
    save_defaults_document,
)
from .farm.state import bag_count_item, cash_count_item, find_summon_slot, monster_snapshot
from .farm.cash import parse_buff_monitor_schedule, parse_cash_schedule
from .farm.restock import parse_restock_schedule, resolve_shop_id
from .farm.stall import stall_status_lines
from .session import run_forever


def _parse_boss_watch(text: str) -> tuple[set[int], list[str]]:
    """把 Boss 监控名单文本(逗号/分号/换行分隔)拆成 (怪种id 集合, 怪名子串列表)。
    纯数字 token 当 type id 精确匹配;其余按怪名子串匹配(如「其他次元」框住整批活动怪)。"""
    ids: set[int] = set()
    names: list[str] = []
    for tok in (text or "").replace(";", ",").replace("\n", ",").replace("\r", ",").split(","):
        t = tok.strip()
        if not t:
            continue
        try:
            ids.add(int(t, 0))
        except ValueError:
            names.append(t)
    return ids, names


def _boss_watch_match(mtype: int, name: str, ids: set[int], names: list[str]) -> bool:
    """该怪是否命中监控名单:怪种 id 命中,或怪名包含任一监控子串。"""
    if mtype in ids:
        return True
    return any(n in name for n in names) if name else False


def _node_cell_text(args, live: dict, pool: list) -> str:
    """该账号当前出口节点的展示文本 `边缘节点IP · 出口地区`,供账号列表「节点/出口」列显示。
    auto 模式读 args._auto_proxy_alias、手动分流读 proxy_host,配 registry 的 node_ip;按别名
    纯函数推车道得出口地区(lane0=直连)。test/默认单实例/尚未选到节点则相应留空或只显示节点IP。"""
    if args is None:
        return ""
    alias = str(getattr(args, "_auto_proxy_alias", "") or "").strip()
    if not alias:
        host = str(getattr(args, "proxy_host", "") or "").strip()
        if host and host.lower() != "auto":
            alias = host
    if not alias:
        return ""
    node_ip = str((live.get(alias) or {}).get("node_ip") or "")
    if not node_ip:
        return ""
    region = ""
    if alias != PROD_PROXY_HOST.strip():
        lane = socks5_pool.lane_for_alias(alias, reserved={PROD_PROXY_HOST.strip()})
        if lane <= 0:
            region = "直连"
        elif lane - 1 < len(pool):
            ent = pool[lane - 1]
            region = ent.get("region") or ent.get("host") or ""
    return f"{node_ip} · {region}" if region else node_ip


# 账号行里由表格列直接管理的字段。其余字段(如 proxy_host、按账号的 server_id 等)表格不显示,
# 必须按 iid 单独留存,否则 build_account_args/account_rows 重建行时会把它们丢掉(连接错代理/存盘抹掉)。
_GUI_MANAGED_KEYS = frozenset({"account", "password", "character", "fishing_enabled"})


class OnlineLoginGui:
    def __init__(self, root, args: argparse.Namespace) -> None:
        import tkinter as tk
        from tkinter import ttk

        self.tk = tk
        self.ttk = ttk
        self.root = root
        self.args = args
        self.accounts_file = Path(args.accounts_file)
        self.defaults_file = Path(args.defaults_file)
        self.log_dir = Path(args.log_dir)
        self.log_queue: queue.Queue[str] = queue.Queue()
        self.status_queue: queue.Queue[tuple[str, str, str]] = queue.Queue()
        # One controller per running account row, keyed by its tree iid. This is
        # only ever touched on the Tk thread, so no extra lock is needed.
        self.controllers: dict[str, AccountController] = {}
        # 表格列管不到的账号级字段(proxy_host 等),按 iid 留存,build_account_args/account_rows 时并回。
        self._account_extras: dict[str, dict[str, object]] = {}

        set_log_handler(self.log_queue.put)
        set_status_handler(
            lambda account, character, status: self.status_queue.put((account, character, status))
        )
        self.root.title("SO3D 在线登录")
        self.root.geometry("900x620")
        self.root.minsize(780, 520)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        self.defaults: dict[str, object] = {}
        self._accounts_file_mtime = 0.0
        self.build_ui()
        self.reload_accounts_from_json()
        self.sync_login_concurrency_from_defaults()
        self.sync_proxy_settings_from_defaults()
        self.drain_logs()
        self.root.after(2000, self.poll_external_accounts)

    def build_ui(self) -> None:
        tk = self.tk
        ttk = self.ttk

        outer = ttk.Frame(self.root, padding=10)
        outer.pack(fill=tk.BOTH, expand=True)
        self.outer = outer

        list_frame = ttk.LabelFrame(outer, text="账号列表")
        list_frame.pack(fill=tk.BOTH, expand=False)

        columns = ("account", "password", "character", "online", "fishing", "hp", "location", "coord", "cash_warn", "real_char", "userid", "node")
        self.tree = ttk.Treeview(list_frame, columns=columns, show="headings", height=8)
        self.tree.heading("account", text="账号")
        self.tree.heading("password", text="密码")
        self.tree.heading("character", text="角色名(配置)")
        self.tree.heading("online", text="在线状态")
        self.tree.heading("fishing", text="自动钓鱼")
        self.tree.heading("hp", text="当前HP/最大HP")
        self.tree.heading("location", text="所在位置")
        self.tree.heading("coord", text="坐标")
        self.tree.heading("cash_warn", text="Cash定时")
        self.tree.heading("real_char", text="登录角色")
        self.tree.heading("userid", text="userId")
        self.tree.heading("node", text="节点/出口")
        self.tree.column("account", width=150, anchor=tk.W)
        self.tree.column("password", width=140, anchor=tk.W)
        self.tree.column("character", width=130, anchor=tk.W)
        self.tree.column("online", width=90, anchor=tk.CENTER)
        self.tree.column("fishing", width=90, anchor=tk.CENTER)
        self.tree.column("hp", width=120, anchor=tk.CENTER)
        self.tree.column("location", width=120, anchor=tk.CENTER)
        self.tree.column("coord", width=90, anchor=tk.CENTER)
        self.tree.column("cash_warn", width=150, anchor=tk.W)
        self.tree.column("real_char", width=130, anchor=tk.W)
        self.tree.column("userid", width=90, anchor=tk.CENTER)
        self.tree.column("node", width=180, anchor=tk.W)
        self.tree.tag_configure("online", foreground="#1a7f37")
        self.tree.tag_configure("offline", foreground="#999999")
        self.tree.tag_configure("connecting", foreground="#b8860b")
        # cash 定时物品缺货/低库存:红底深红字,盖过在线/离线配色,醒目
        self.tree.tag_configure("cash_alert", background="#ffd6d6", foreground="#b00020")
        self.tree.bind("<Button-1>", self.on_tree_click)
        self.tree.bind("<Button-3>", self.on_tree_right_click)
        self.tree.bind("<<TreeviewSelect>>", self.on_tree_select)
        self.row_menu = tk.Menu(self.tree, tearoff=0)
        self.row_menu.add_command(label="启动该账号", command=self.start_selected)
        self.row_menu.add_command(label="停止该账号", command=self.stop_selected)
        self.row_menu.add_separator()
        self.row_menu.add_command(label="查看cashbuff", command=self.open_cashbuff_view)
        self.row_menu.add_command(label="周围怪物", command=self.open_monster_view)
        self.row_menu.add_command(label="回城复活", command=self.revive_selected)
        self.row_menu.add_command(label="开始钓鱼", command=self.start_fishing_selected)
        self.row_menu.add_command(label="停止钓鱼", command=self.stop_fishing_selected)
        self.row_menu.add_command(label="坐标传送…", command=self.warp_selected)
        self.row_menu.add_separator()
        self.row_menu.add_command(label="摆摊…", command=self.open_stall_view)
        self.row_menu.add_command(label="停止摆摊", command=self.stop_stall_selected)
        self.row_menu.add_separator()
        self.row_menu.add_command(label="批量邮寄物品…", command=self.batch_mail_selected)
        scroll = ttk.Scrollbar(list_frame, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        self.tree.grid(row=0, column=0, sticky="nsew")
        scroll.grid(row=0, column=1, sticky="ns")
        list_frame.columnconfigure(0, weight=1)
        list_frame.rowconfigure(0, weight=1)

        detail_frame = ttk.LabelFrame(outer, text="选中账号统计(本次会话)")
        detail_frame.pack(fill=tk.X, pady=(8, 0))
        self.detail_var = tk.StringVar(value="（在上方列表选中一个账号查看统计）")
        ttk.Label(detail_frame, textvariable=self.detail_var, justify=tk.LEFT,
                  anchor=tk.W, padding=(8, 6)).pack(fill=tk.X)

        edit = ttk.Frame(outer)
        edit.pack(fill=tk.X, pady=(8, 6))
        self.account_var = tk.StringVar()
        self.password_var = tk.StringVar()
        self.character_var = tk.StringVar()
        ttk.Label(edit, text="账号").grid(row=0, column=0, sticky=tk.W)
        ttk.Entry(edit, textvariable=self.account_var, width=20).grid(row=1, column=0, sticky="ew", padx=(0, 8))
        ttk.Label(edit, text="密码").grid(row=0, column=1, sticky=tk.W)
        ttk.Entry(edit, textvariable=self.password_var, width=20).grid(row=1, column=1, sticky="ew", padx=(0, 8))
        ttk.Label(edit, text="角色名").grid(row=0, column=2, sticky=tk.W)
        ttk.Entry(edit, textvariable=self.character_var, width=20).grid(row=1, column=2, sticky="ew", padx=(0, 8))
        self.add_button = ttk.Button(edit, text="添加", command=self.add_account)
        self.add_button.grid(row=1, column=3, padx=(0, 6))
        self.delete_button = ttk.Button(edit, text="删除选中", command=self.delete_selected)
        self.delete_button.grid(row=1, column=4, padx=(0, 6))
        self.start_button = ttk.Button(edit, text="全部启动", command=self.start)
        self.start_button.grid(row=1, column=5, padx=(0, 6))
        self.stop_button = ttk.Button(edit, text="全部停止", command=self.stop)
        self.stop_button.grid(row=1, column=6, padx=(0, 6))
        self.start_sel_button = ttk.Button(edit, text="启动选中", command=self.start_selected)
        self.start_sel_button.grid(row=1, column=7, padx=(0, 6))
        self.stop_sel_button = ttk.Button(edit, text="停止选中", command=self.stop_selected)
        self.stop_sel_button.grid(row=1, column=8, padx=(0, 6))
        self.farm_button = ttk.Button(edit, text="挂机设置", command=self.open_farm_settings)
        self.farm_button.grid(row=1, column=9, padx=(0, 6))
        self.bag_button = ttk.Button(edit, text="查看背包", command=self.open_bag_view)
        self.bag_button.grid(row=1, column=10)
        self.boss_button = ttk.Button(edit, text="Boss监控", command=self.toggle_boss_monitor)
        self.boss_button.grid(row=2, column=4, columnspan=2, sticky=tk.W, padx=(12, 0), pady=(6, 0))

        # 服务器模式:test=直连公网真服(现有逻辑); prod=连本地代理 + 123.dll bridge 解密。
        mode_init = str(getattr(self.args, "server_mode", "test") or "test").strip().lower()
        if mode_init not in ("test", "prod"):
            mode_init = "test"
        self.server_mode_var = tk.StringVar(value="正式服" if mode_init == "prod" else "测试服")
        ttk.Label(edit, text="服务器").grid(row=2, column=0, sticky=tk.W, pady=(6, 0))
        self.server_mode_combo = ttk.Combobox(
            edit, textvariable=self.server_mode_var, state="readonly",
            values=("测试服", "正式服"), width=8,
        )
        self.server_mode_combo.grid(row=2, column=1, sticky=tk.W, pady=(6, 0))
        self.server_mode_combo.bind("<<ComboboxSelected>>", self.on_server_mode_change)
        # 初始化一次,保证 self.args.server_mode 与下拉一致
        self.args.server_mode = mode_init

        # 同时登录数:限制同时处于登录流程的账号数,超出的排队;一次性启动很多账号时避免登录风暴。
        conc_init = int(getattr(self.args, "login_concurrency", CONFIG["login_concurrency"]) or 1)
        self.login_conc_var = tk.StringVar(value=str(max(1, conc_init)))
        ttk.Label(edit, text="同时登录数").grid(row=2, column=2, sticky=tk.E, pady=(6, 0))
        self.login_conc_spin = ttk.Spinbox(
            edit, from_=1, to=50, width=6, textvariable=self.login_conc_var,
            command=self.on_login_concurrency_change,
        )
        self.login_conc_spin.grid(row=2, column=3, sticky=tk.W, pady=(6, 0))
        self.login_conc_spin.bind("<Return>", self.on_login_concurrency_change)
        self.login_conc_spin.bind("<FocusOut>", self.on_login_concurrency_change)
        LOGIN_GATE.set_limit(max(1, conc_init))

        # 自动分流(仅正式服生效):勾选=把账号的 proxy_host 设为 "auto",登录时按「每出口上限」
        # 自动拉起 port_proxy.py 代理实例;每装满一个出口就自动开下一个、绑到 socks5_pool.txt
        # 里的下一条 SOCKS5(经 mihomo TUN 按进程名导到不同真实出口IP,绕开单出口IP总并发~77 的上限)。
        auto_init = str(getattr(self.args, "proxy_host", "") or "").strip().lower() == "auto"
        self.auto_proxy_var = tk.BooleanVar(value=auto_init)
        ttk.Label(edit, text="自动分流").grid(row=3, column=0, sticky=tk.W, pady=(6, 0))
        self.auto_proxy_check = ttk.Checkbutton(
            edit, variable=self.auto_proxy_var, command=self.on_auto_proxy_change,
        )
        self.auto_proxy_check.grid(row=3, column=1, sticky=tk.W, pady=(6, 0))
        # 每出口上限:每条出口(直连/每个 SOCKS5)最多挂多少账号;到达即自动开下一个出口。
        # 建议 <77(实测单出口IP总并发天花板),如 70。0=不限(只用一条出口,不分流)。
        cap_init = int(getattr(self.args, "proxy_max_per_node", CONFIG["proxy_max_per_node"]) or 0)
        self.proxy_cap_var = tk.StringVar(value=str(max(0, cap_init)))
        ttk.Label(edit, text="每出口上限").grid(row=3, column=2, sticky=tk.E, pady=(6, 0))
        self.proxy_cap_spin = ttk.Spinbox(
            edit, from_=0, to=999, width=6, textvariable=self.proxy_cap_var,
            command=self.on_proxy_cap_change,
        )
        self.proxy_cap_spin.grid(row=3, column=3, sticky=tk.W, pady=(6, 0))
        self.proxy_cap_spin.bind("<Return>", self.on_proxy_cap_change)
        self.proxy_cap_spin.bind("<FocusOut>", self.on_proxy_cap_change)
        # 出口池录入 + 一键生成 Clash(mihomo)配置。
        ttk.Button(edit, text="编辑出口池…", command=self.on_edit_socks5_pool).grid(
            row=4, column=0, columnspan=2, sticky=tk.W, pady=(4, 0))
        ttk.Button(edit, text="生成Clash配置", command=self.on_gen_clash).grid(
            row=4, column=2, columnspan=2, sticky=tk.W, pady=(4, 0))

        for index in range(3):
            edit.columnconfigure(index, weight=1)

        self.bottom_nb = ttk.Notebook(outer)
        self.bottom_nb.pack(fill=tk.BOTH, expand=True)

        log_frame = ttk.Frame(self.bottom_nb)
        self.bottom_nb.add(log_frame, text="日志")
        self.log_frame = log_frame
        self.log_text = tk.Text(log_frame, height=18, wrap=tk.NONE, state=tk.DISABLED)
        # 日志按事件类型上色:登录成功=绿;chat(3000)重连/secondary 失败=黄;主端口 reset/僵尸进场=红。
        # 颜色与上方账号列表标签一致(见 tree.tag_configure)。
        self.log_text.tag_configure("ok", foreground="#1a7f37")
        self.log_text.tag_configure("warn", foreground="#b8860b")
        self.log_text.tag_configure("err", foreground="#b00020")
        yscroll = ttk.Scrollbar(log_frame, orient=tk.VERTICAL, command=self.log_text.yview)
        xscroll = ttk.Scrollbar(log_frame, orient=tk.HORIZONTAL, command=self.log_text.xview)
        self.log_text.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

        boss_tab = ttk.Frame(self.bottom_nb)
        self.bottom_nb.add(boss_tab, text="Boss监控")
        self._boss_tab = boss_tab
        self._build_boss_panel(boss_tab)
        # 只在切到 Boss 监控页时跑 2s 刷新定时器,切走就停掉省 CPU。
        self.bottom_nb.bind("<<NotebookTabChanged>>", self._on_bottom_tab_changed)

    def insert_account(self, account: dict[str, object]) -> str:
        fishing_on = _as_bool(account.get("fishing_enabled", False))
        iid = self.tree.insert(
            "",
            self.tk.END,
            values=(
                str(account.get("account") or ""),
                str(account.get("password") or ""),
                str(account.get("character") or ""),
                "",
                FISHING_ON_MARK if fishing_on else FISHING_OFF_MARK,
                "",
                "",
                "",
                "",
                "",
                "",
                "",
            ),
            tags=("offline",),
        )
        # 留存表格列之外的账号级字段(proxy_host 等),供启动/存盘时并回。
        extras = {k: v for k, v in account.items() if k not in _GUI_MANAGED_KEYS}
        if extras:
            self._account_extras[iid] = extras
        return iid

    def clear_accounts(self) -> None:
        for iid in self.tree.get_children():
            self.tree.delete(iid)
        self._account_extras.clear()

    def reload_accounts_from_json(self) -> list[dict[str, object]]:
        self.defaults, accounts = load_accounts_document(self.accounts_file, self.defaults_file)
        self.clear_accounts()
        for account in accounts:
            self.insert_account(account)
        self._accounts_file_mtime = self._accounts_file_stat_mtime()
        # build_ui 建 Boss 面板时 self.defaults 还是空的,defaults 到位后把 boss_watch 灌进名单框。
        self._sync_boss_watch_ui()
        return accounts

    def save_accounts_from_tree(self) -> None:
        save_accounts_document(self.accounts_file, self.account_rows())
        # 记下自己刚写完的 mtime,避免外部账号轮询把自身写入误判成「外部新增」。
        self._accounts_file_mtime = self._accounts_file_stat_mtime()
        main_log(f"[界面] 已保存账号配置到 {self.accounts_file}")

    def _accounts_file_stat_mtime(self) -> float:
        try:
            return self.accounts_file.stat().st_mtime if self.accounts_file.exists() else 0.0
        except OSError:
            return 0.0

    def poll_external_accounts(self) -> None:
        """每 2s 看一眼 accounts 文件的 mtime;变了就把外部进程(register.py/--add-account)
        新追加的账号并进列表。并入只读不覆盖,既让新账号实时出现,又防止下一次 GUI 保存把
        外部追加的账号整段冲掉。"""
        mtime = self._accounts_file_stat_mtime()
        if mtime != self._accounts_file_mtime:
            self._accounts_file_mtime = mtime
            self.merge_external_accounts()
        self.root.after(2000, self.poll_external_accounts)

    def merge_external_accounts(self) -> None:
        try:
            _defaults, accounts = load_accounts_document(self.accounts_file, self.defaults_file)
        except Exception:
            return
        existing = {str(self.tree.item(iid, "values")[0]) for iid in self.tree.get_children()}
        added = 0
        for account in accounts:
            name = str(account.get("account") or "")
            if name and name not in existing:
                self.insert_account(account)
                existing.add(name)
                added += 1
        if added:
            main_log(f"[界面] 检测到外部新增 {added} 个账号，已并入列表（默认不登录）")

    def save_defaults_to_file(self) -> None:
        save_defaults_document(self.defaults_file, self.defaults)
        main_log(f"[界面] 已保存挂机默认配置到 {self.defaults_file}")

    def account_rows(self) -> list[dict[str, object]]:
        rows: list[dict[str, object]] = []
        for iid in self.tree.get_children():
            values = self.tree.item(iid, "values")
            account, password, character = values[0], values[1], values[2]
            fishing_on = len(values) >= 5 and values[4] == FISHING_ON_MARK
            # 先铺表格管不到的字段(proxy_host 等),再写列字段,避免存盘把自定义字段抹掉。
            row: dict[str, object] = dict(self._account_extras.get(iid, {}))
            row.update({
                "account": account,
                "password": password,
                "character": character,
                "fishing_enabled": fishing_on,
            })
            rows.append(row)
        return rows

    def on_tree_click(self, event) -> None:
        # 仅「自动钓鱼」列(#5)的单击切换勾选,其它列保持默认选中行为。
        if self.tree.identify_region(event.x, event.y) != "cell":
            return
        if self.tree.identify_column(event.x) != "#5":
            return
        iid = self.tree.identify_row(event.y)
        if not iid:
            return
        values = list(self.tree.item(iid, "values"))
        while len(values) < 5:
            values.append("")
        values[4] = FISHING_OFF_MARK if values[4] == FISHING_ON_MARK else FISHING_ON_MARK
        self.tree.item(iid, values=values)
        self.save_accounts_from_tree()
        if iid in self.controllers:
            main_log("[界面] 自动钓鱼开关已改，对该账号重启后生效")

    def add_account(self) -> None:
        account = self.account_var.get().strip()
        password = self.password_var.get().strip()
        character = self.character_var.get().strip()
        if not account or not password or not character:
            main_log("[界面] 账号、密码、角色名不能为空")
            return
        row = {"account": account, "password": password, "character": character}
        self.insert_account(row)
        self.account_var.set("")
        self.password_var.set("")
        self.character_var.set("")
        self.save_accounts_from_tree()
        # 新加的账号默认不自动登录,只入列表;需要时再手动「启动选中」。
        main_log(f"[界面] 已添加账号 {account}（默认不登录，需手动启动）")

    def delete_selected(self) -> None:
        for iid in self.tree.selection():
            # Deleting a row auto-disconnects its session if it is running.
            self.stop_account_row(iid)
            self.tree.delete(iid)
            self._account_extras.pop(iid, None)
        self.save_accounts_from_tree()

    def on_login_concurrency_change(self, _event=None) -> None:
        """改「同时登录数」:校正到 1..50,热更新全局登录闸门,并随挂机默认一起持久化。"""
        try:
            value = int(float(self.login_conc_var.get().strip()))
        except (TypeError, ValueError):
            value = LOGIN_GATE.limit
        value = max(1, min(50, value))
        if str(value) != self.login_conc_var.get():
            self.login_conc_var.set(str(value))
        if value == LOGIN_GATE.limit and self.defaults.get("login_concurrency") == value:
            return
        LOGIN_GATE.set_limit(value)
        self.args.login_concurrency = value
        self.defaults["login_concurrency"] = value
        try:
            save_defaults_document(self.defaults_file, self.defaults)
        except Exception:
            pass
        main_log(f"[界面] 同时登录数已设为 {value}(超出的账号登录时排队)")

    def sync_login_concurrency_from_defaults(self) -> None:
        """从挂机默认文件读回上次保存的「同时登录数」(var.set 不触发回写,无循环)。"""
        raw = self.defaults.get("login_concurrency")
        if raw is None:
            return
        try:
            value = max(1, min(50, int(float(raw))))
        except (TypeError, ValueError):
            return
        self.login_conc_var.set(str(value))
        self.args.login_concurrency = value
        LOGIN_GATE.set_limit(value)

    def on_auto_proxy_change(self) -> None:
        """勾选/取消「自动分流」:写进全局默认的 proxy_host(auto/空)并持久化。仅正式服生效。"""
        on = bool(self.auto_proxy_var.get())
        host = "auto" if on else ""
        self.args.proxy_host = host
        self.defaults["proxy_host"] = host
        try:
            save_defaults_document(self.defaults_file, self.defaults)
        except Exception:
            pass
        if on:
            cap = int(getattr(self.args, "proxy_max_per_node", 0) or 0)
            main_log("[界面] 已开启自动分流:正式服登录时自动拉起代理、按出口分摊并经 mihomo TUN 走不同出口IP")
            if cap <= 0:
                main_log("[界面] 提示:当前「每出口上限」为 0(不限),只会用一条出口。要分流请设成 >0(如 70)")
        else:
            main_log("[界面] 已关闭自动分流:正式服走单一默认代理")

    def on_proxy_cap_change(self, _event=None) -> None:
        """改「每出口上限」:校正到 0..999,写进全局默认的 proxy_max_per_node 并持久化。
        既是每条出口(直连/每个SOCKS5)账号上限,也是「到此值就自动换下一条出口」的阈值;0=不限。"""
        try:
            value = int(float(self.proxy_cap_var.get().strip()))
        except (TypeError, ValueError):
            value = int(getattr(self.args, "proxy_max_per_node", 0) or 0)
        value = max(0, min(999, value))
        if str(value) != self.proxy_cap_var.get():
            self.proxy_cap_var.set(str(value))
        if self.defaults.get("proxy_max_per_node") == value and \
                int(getattr(self.args, "proxy_max_per_node", 0) or 0) == value:
            return
        self.args.proxy_max_per_node = value
        self.defaults["proxy_max_per_node"] = value
        try:
            save_defaults_document(self.defaults_file, self.defaults)
        except Exception:
            pass
        main_log(f"[界面] 每出口上限已设为 {value}(0=不限;到达即自动换下一条出口/SOCKS5)")

    def on_edit_socks5_pool(self) -> None:
        """打开 socks5_pool.txt 录入可用 SOCKS5 出口(不存在则先写带说明的模板)。"""
        try:
            path = socks5_pool.ensure_pool_file_template()
            os.startfile(str(path))  # noqa: S606 - Windows 用默认程序打开文本
            main_log(f"[界面] 已打开出口池:{path}(每行 host:port:user:pass[:地区],改完保存)")
        except Exception as exc:  # noqa: BLE001
            main_log(f"[界面] 打开出口池失败:{exc}")

    def on_gen_clash(self) -> None:
        """按出口池生成 Clash(mihomo,TUN 模式)配置并打开所在文件夹。bot 登录时会自动加载它,
        此按钮仅供手动预览/排查。"""
        try:
            out = socks5_pool.gen_clash_config()
        except Exception as exc:  # noqa: BLE001
            main_log(f"[界面] 生成 Clash 配置失败:{exc}")
            return
        if out is None:
            main_log("[界面] 出口池为空,未生成 Clash 配置;请先「编辑出口池…」添加 SOCKS5")
            return
        try:
            os.startfile(str(Path(out).parent))  # noqa: S606 - 打开所在文件夹便于查看
        except Exception:  # noqa: BLE001
            pass
        main_log(f"[界面] Clash 配置已生成:{out}。登录正式服并开启自动分流后,bot 会自动用 "
                 "mihomo 核心(TUN)加载它,无需手动操作。")

    def sync_proxy_settings_from_defaults(self) -> None:
        """从挂机默认文件读回上次保存的自动分流开关与每节点上限(var.set 不触发回写,无循环)。"""
        raw_host = self.defaults.get("proxy_host")
        if raw_host is not None:
            on = str(raw_host).strip().lower() == "auto"
            self.auto_proxy_var.set(on)
            self.args.proxy_host = "auto" if on else str(raw_host)
        raw_cap = self.defaults.get("proxy_max_per_node")
        if raw_cap is not None:
            try:
                value = max(0, min(999, int(float(raw_cap))))
            except (TypeError, ValueError):
                value = 0
            self.proxy_cap_var.set(str(value))
            self.args.proxy_max_per_node = value

    def current_server_mode(self) -> str:
        return "prod" if self.server_mode_var.get().strip() == "正式服" else "test"

    def on_server_mode_change(self, _event=None) -> None:
        mode = self.current_server_mode()
        self.args.server_mode = mode
        if mode == "prod":
            main_log("[界面] 已切换到正式服(连本地代理 127.2.57.25 + 123.dll bridge 解密)")
            # 切换即时探测一次,给用户提前反馈(不阻止切换)
            ok, detail = self._probe_prod_bridge()
            if ok:
                main_log(f"[界面] 检测到可用 bridge:{detail}")
            else:
                main_log(f"[界面] 警告:暂未检测到可用 bridge({detail});启动正式服前请先登录一个游戏进程")
        else:
            main_log("[界面] 已切换到测试服(直连公网真服)")

    def _probe_prod_bridge(self) -> tuple[bool, str]:
        """探测是否有就绪的 123.dll 解密 bridge。返回 (ok, 描述)。"""
        if bridge_client is None:
            return False, "bridge_client 模块未导入"
        try:
            bridge = bridge_client.connect_ready_bridge()
        except Exception as exc:
            return False, str(exc)
        pid = bridge.pid
        bridge.close()
        return True, f"pid={pid}"

    def _ensure_prod_bridge_or_warn(self) -> bool:
        """正式服启动前置检查:无可用 bridge 则弹窗提示并返回 False(中止本次登录)。"""
        if self.current_server_mode() != "prod":
            return True
        ok, detail = self._probe_prod_bridge()
        if ok:
            return True
        from tkinter import messagebox
        messagebox.showwarning(
            "正式服无法登录",
            "未检测到可用的 123.dll 解密 bridge,无法登录正式服。\n\n"
            "请先启动并登录一个注入了 GGThreadBlock 的游戏进程,\n"
            "进入游戏后再点击启动。\n\n"
            f"详情:{detail}",
        )
        main_log(f"[界面] 正式服启动中止:{detail}")
        return False

    def build_account_args(self, iid: str) -> argparse.Namespace:
        values = self.tree.item(iid, "values")
        # 先铺表格列之外的账号级字段(proxy_host 等),否则会被全局默认盖掉、连错代理。
        account = dict(self._account_extras.get(iid, {}))
        account.update({"account": values[0], "password": values[1], "character": values[2]})
        # 「自动钓鱼」列(values[4])是该账号开关的真实来源,必须并进 account dict,
        # 否则 account_namespace 里它会被全局默认 fishing_enabled 盖掉(行内关了仍会钓鱼)。
        if len(values) >= 5:
            account["fishing_enabled"] = values[4] == FISHING_ON_MARK
        # GUI 下拉的服务器模式优先于 defaults.json,强制写进 account 覆盖层。
        account["server_mode"] = self.current_server_mode()
        account_args = account_namespace(self.args, self.defaults, account)
        missing = [name for name in ("account", "password", "character") if not getattr(account_args, name, "")]
        if missing:
            raise ValueError("缺少字段：" + ", ".join(missing))
        return account_args

    def apply_farm_settings_to_running(self) -> int:
        """把刚改的全局挂机设置就地写进每个在线账号的实时 args。worker 主循环每 0.25s tick
        现读 args.fishing_enabled/autosell_*/autobuy_* 等,故无需重启即生效;account_args 跨
        重连复用同一对象,改动也会保留。用 build_account_args 重新合并,保证账号级覆盖仍优先于
        全局默认;只覆盖 FARM_FIELD_SPECS 的键,连接类参数(端口/server_id)不动。返回热更新账号数。"""
        count = 0
        for iid, controller in self.controllers.items():
            live = getattr(controller, "args", None)
            if live is None:
                continue
            try:
                rebuilt = self.build_account_args(iid)
            except Exception:
                continue
            for key, _label, _kind in FARM_FIELD_SPECS:
                if hasattr(rebuilt, key):
                    setattr(live, key, getattr(rebuilt, key))
            # 这些键不在 FARM_FIELD_SPECS(列表/字符串/自动邮寄 tab 自管),单独热更新。
            for extra in ("cash_use_schedule", "buff_monitor_schedule", "automail_enabled",
                          "automail_recipient", "automail_money_threshold", "automail_amount",
                          "automail_poll_interval", "automail_whitelist", "restock_enabled",
                          "restock_schedule"):
                if hasattr(rebuilt, extra):
                    setattr(live, extra, getattr(rebuilt, extra))
            count += 1
        return count

    def start_account_row(self, iid: str) -> None:
        if iid in self.controllers:
            return
        try:
            account_args = self.build_account_args(iid)
        except Exception:
            main_log("[界面] 启动账号失败")
            for line in traceback.format_exc().rstrip().splitlines():
                main_log(line)
            return

        controller = AccountController(account_args.account, account_args.character)
        controller.args = account_args   # 「挂机设置」热更新就地改写这个对象
        self.controllers[iid] = controller

        def worker() -> None:
            try:
                run_forever(
                    account_args,
                    self.log_dir,
                    retry_delay=float(getattr(account_args, "retry_delay", self.args.retry_delay)),
                    max_retries=int(getattr(account_args, "max_retries", self.args.max_retries)),
                    failure_window_seconds=float(
                        getattr(account_args, "failure_window_seconds", self.args.failure_window_seconds)
                    ),
                    max_failures_per_window=int(
                        getattr(account_args, "max_failures_per_window", self.args.max_failures_per_window)
                    ),
                    console=False,
                    controller=controller,
                )
            except Exception:
                main_log(f"[界面] 账号 {account_args.account} 线程异常")
                for line in traceback.format_exc().rstrip().splitlines():
                    main_log(line)
            finally:
                self.root.after(0, lambda: self.controllers.pop(iid, None))

        controller.thread = threading.Thread(
            target=worker,
            name=f"so3d-{account_args.account}-{account_args.character}",
            daemon=True,
        )
        controller.thread.start()

    def stop_account_row(self, iid: str) -> None:
        controller = self.controllers.pop(iid, None)
        if controller is None:
            return
        main_log(f"[界面] 正在断开账号 {controller.account}")

        def closer() -> None:
            controller.stop_event.set()
            for sock in controller.take_sockets():
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass

        threading.Thread(target=closer, daemon=True).start()

    def start(self) -> None:
        rows = self.tree.get_children()
        if not rows:
            main_log("[界面] 没有可启动的账号")
            return
        if not self._ensure_prod_bridge_or_warn():
            return
        for iid in rows:
            self.start_account_row(iid)

    def stop(self) -> None:
        for iid in list(self.controllers.keys()):
            self.stop_account_row(iid)

    def selected_rows(self) -> list[str]:
        return list(self.tree.selection())

    def start_selected(self) -> None:
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        if not self._ensure_prod_bridge_or_warn():
            return
        for iid in rows:
            self.start_account_row(iid)

    def stop_selected(self) -> None:
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        for iid in rows:
            self.stop_account_row(iid)

    def revive_selected(self) -> None:
        """对选中(可多选)且在线的账号回城复活:弹窗选落点城镇(乐园镇=mode1 / 狮子城=mode2,
        对齐 GGThreadBlock TeleportTownModule),选中后对所有选中在线账号投递 ("revive", mode),
        worker 线程发 CG_PLAYER_REVIVE(412017,body=单 DWORD reviveMode)。"""
        tk = self.tk
        ttk = self.ttk
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        online: list["AccountController"] = []
        for iid in rows:
            controller = self.controllers.get(iid)
            if controller is None or controller.farm is None:
                vals = self.tree.item(iid, "values")
                who = vals[0] if vals else iid
                main_log(f"[界面] 账号 {who} 未在线,无法回城复活")
                continue
            online.append(controller)
        if not online:
            main_log("[界面] 选中的账号都不在线,无法回城复活")
            return

        dlg = tk.Toplevel(self.root)
        dlg.title("回城复活 - 选择落点")
        dlg.transient(self.root)
        dlg.grab_set()
        frm = ttk.Frame(dlg, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)
        ttk.Label(frm, text=f"对 {len(online)} 个在线账号回城复活,选落点城镇:").pack(anchor=tk.W, pady=(0, 8))

        def _do_revive(town: str, mode: int) -> None:
            for controller in online:
                controller.action_queue.put(("revive", mode))
            main_log(f"[界面] 已请求回城复活 x{len(online)} -> {town}(CG_PLAYER_REVIVE 412017 mode={mode})")
            dlg.destroy()

        btns = ttk.Frame(frm)
        btns.pack(fill=tk.X)
        for town, mode in REVIVE_TOWNS:
            ttk.Button(btns, text=town, width=12,
                       command=lambda t=town, m=mode: _do_revive(t, m)).pack(side=tk.LEFT, padx=(0, 8))
        ttk.Label(frm, text="落点由服务端按 reviveMode 决定;瞬间回城。",
                  foreground="#666666").pack(anchor=tk.W, pady=(8, 0))
        ttk.Button(frm, text="取消", command=dlg.destroy).pack(anchor=tk.E, pady=(10, 0))

    def start_fishing_selected(self) -> None:
        """对选中(可多选)且在线的账号投递 fishing_start:worker 线程停竿->等1s->重抛,
        并打开 fishing_enabled 让 fishing_tick 之后按间隔定时发中鱼/收线包。"""
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        sent = 0
        for iid in rows:
            controller = self.controllers.get(iid)
            if controller is None or controller.farm is None:
                vals = self.tree.item(iid, "values")
                who = vals[0] if vals else iid
                main_log(f"[界面] 账号 {who} 未在线,无法开始钓鱼")
                continue
            controller.action_queue.put("fishing_start")
            sent += 1
        if sent:
            main_log(f"[界面] 已请求开始钓鱼 x{sent}(停竿->等1s->重抛->定时中鱼)")

    def stop_fishing_selected(self) -> None:
        """对选中(可多选)且在线的账号投递 fishing_stop:worker 线程关掉 fishing_enabled(不再定时收线)
        并发停竿包(411023)取消当前抛竿动作。"""
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        sent = 0
        for iid in rows:
            controller = self.controllers.get(iid)
            if controller is None or controller.farm is None:
                vals = self.tree.item(iid, "values")
                who = vals[0] if vals else iid
                main_log(f"[界面] 账号 {who} 未在线,无法停止钓鱼")
                continue
            controller.action_queue.put("fishing_stop")
            sent += 1
        if sent:
            main_log(f"[界面] 已请求停止钓鱼 x{sent}(关闭定时收线并停竿)")

    def warp_selected(self) -> None:
        """对选中(可多选)且在线的账号发坐标传送:弹框填 X/Y(可「填当前」取首个在线账号的
        当前坐标),确认后走 action_queue 在 worker 线程发 CG_STREETSTALL_WARP(411597)瞬移。
        x/y 为整数地图格坐标(非世界浮点),对齐街摊「传送到摊主」/ GGThreadBlock::WarpToCoordinate。"""
        tk = self.tk
        ttk = self.ttk
        from tkinter import messagebox
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        # 只对在线账号发;顺带取首个在线账号已回流的当前坐标作为「填当前」默认值。
        online: list["AccountController"] = []
        cur_x = cur_y = None
        for iid in rows:
            controller = self.controllers.get(iid)
            if controller is None or controller.farm is None:
                continue
            online.append(controller)
            if cur_x is None and getattr(controller.farm, "coord_seen", False):
                cur_x, cur_y = controller.farm.coord_x, controller.farm.coord_y
        if not online:
            main_log("[界面] 选中的账号都不在线,无法坐标传送")
            return

        dlg = tk.Toplevel(self.root)
        dlg.title("坐标传送")
        dlg.transient(self.root)
        dlg.grab_set()
        frm = ttk.Frame(dlg, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)
        xv = tk.StringVar(value=str(cur_x) if cur_x is not None else "")
        yv = tk.StringVar(value=str(cur_y) if cur_y is not None else "")
        ttk.Label(frm, text=f"将对 {len(online)} 个在线账号发坐标传送").grid(
            row=0, column=0, columnspan=3, sticky=tk.W, pady=(0, 6))
        ttk.Label(frm, text="X(地图格)").grid(row=1, column=0, sticky=tk.W, pady=3)
        ttk.Entry(frm, textvariable=xv, width=10).grid(row=1, column=1, sticky="ew", padx=(8, 0))
        ttk.Label(frm, text="Y(地图格)").grid(row=2, column=0, sticky=tk.W, pady=3)
        ttk.Entry(frm, textvariable=yv, width=10).grid(row=2, column=1, sticky="ew", padx=(8, 0))
        cur_text = f"当前坐标:({cur_x}, {cur_y})" if cur_x is not None else "当前坐标:未知(尚未回流)"

        def _fill_cur() -> None:
            if cur_x is not None:
                xv.set(str(cur_x))
                yv.set(str(cur_y))

        ttk.Button(frm, text="填当前", command=_fill_cur).grid(row=1, column=2, rowspan=2, padx=(8, 0))
        ttk.Label(frm, text=cur_text).grid(row=3, column=0, columnspan=3, sticky=tk.W, pady=(6, 0))
        ttk.Label(frm, text="整数地图格坐标(非世界浮点);瞬移不走路。",
                  foreground="#666666").grid(row=4, column=0, columnspan=3, sticky=tk.W, pady=(2, 0))
        frm.columnconfigure(1, weight=1)

        def do_warp() -> None:
            try:
                x = int(str(xv.get()).strip())
                y = int(str(yv.get()).strip())
            except ValueError:
                messagebox.showwarning("坐标传送", "X/Y 必须是整数")
                return
            for controller in online:
                controller.action_queue.put(("warp", x, y))
            main_log(f"[界面] 已请求坐标传送 x{len(online)} -> ({x}, {y})(发送 CG_STREETSTALL_WARP 411597)")
            dlg.destroy()

        bb = ttk.Frame(frm)
        bb.grid(row=5, column=0, columnspan=3, sticky="ew", pady=(10, 0))
        ttk.Button(bb, text="传送", command=do_warp).pack(side=tk.RIGHT)
        ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

    def batch_mail_selected(self) -> None:
        """对选中(可多选)且在线的账号:在各自背包搜同名物品,整格全寄给指定收件人。
        收件人本人那个账号会被跳过(不寄给自己)。物品邮件走 CG_SEND_MAIL op=1,
        itemBagId=wireSlot(内部slot+13),整格全寄。确认后逐格投递 ("mail_item", recipient, slot, iid)。"""
        from tkinter import messagebox
        tk = self.tk
        ttk = self.ttk
        rows = self.selected_rows()
        if not rows:
            main_log("[界面] 未选中任何账号")
            return
        online: list["AccountController"] = []
        for iid in rows:
            controller = self.controllers.get(iid)
            if controller is None or controller.farm is None:
                vals = self.tree.item(iid, "values")
                who = vals[0] if vals else iid
                main_log(f"[界面] 账号 {who} 未在线,跳过批量邮寄")
                continue
            online.append(controller)
        if not online:
            main_log("[界面] 选中的账号都不在线,无法批量邮寄")
            return

        dlg = tk.Toplevel(self.root)
        dlg.title("批量邮寄物品")
        dlg.transient(self.root)
        dlg.grab_set()
        frm = ttk.Frame(dlg, padding=12)
        frm.pack(fill=tk.BOTH, expand=True)
        ttk.Label(frm, text=f"从 {len(online)} 个在线账号的背包搜同名物品,整格寄给:").grid(
            row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
        ttk.Label(frm, text="收件人角色名").grid(row=1, column=0, sticky=tk.W, pady=3)
        rv = tk.StringVar()
        ttk.Entry(frm, textvariable=rv, width=24).grid(row=1, column=1, sticky="ew", padx=(8, 0))
        ttk.Label(frm, text="物品名(逗号/换行分隔)").grid(row=2, column=0, sticky=tk.NW, pady=3)
        txt = tk.Text(frm, width=28, height=6)
        txt.grid(row=2, column=1, sticky="ew", padx=(8, 0))
        ttk.Label(frm, text="整格全寄,直接真寄,不可逆!", foreground="#b00020").grid(
            row=3, column=0, columnspan=2, sticky=tk.W, pady=(6, 0))
        frm.columnconfigure(1, weight=1)

        def do_send() -> None:
            recipient = rv.get().strip()
            raw = txt.get("1.0", tk.END)
            tokens = [t.strip() for t in raw.replace("，", ",").replace("\n", ",").split(",") if t.strip()]
            if not recipient:
                messagebox.showwarning("批量邮寄", "收件人不能为空")
                return
            if not tokens:
                messagebox.showwarning("批量邮寄", "请至少输入一个物品名")
                return
            target_ids: set[int] = set()
            target_names: set[str] = set()
            for tok in tokens:
                tid = resolve_item_id(tok, 0, warn=False)
                if tid:
                    target_ids.add(tid)
                else:
                    target_names.add(tok)
            plan: list[tuple["AccountController", int, int]] = []  # (controller, slot, iid)
            preview_lines: list[str] = []
            skipped_self = 0
            for controller in online:
                farm = controller.farm
                if farm is None:
                    continue
                own = str(getattr(farm, "real_character", "") or controller.character or "").strip()
                if recipient == own:
                    skipped_self += 1
                    continue
                for slot in range(BAG_MAIN_SLOTS):
                    iid, cnt = farm.bag.get(slot, (0, 0))
                    if iid == 0 or cnt <= 0:
                        continue
                    if iid in target_ids or item_name(iid) in target_names:
                        plan.append((controller, slot, iid))
                        preview_lines.append(f"  [{own or controller.character}] 格{slot} {item_label(iid)} x{cnt}")
            if not plan:
                messagebox.showinfo(
                    "批量邮寄",
                    "所有在线账号背包里都没找到同名物品" +
                    (f"(另跳过 {skipped_self} 个=收件人本人)" if skipped_self else ""))
                return
            preview = "\n".join(preview_lines[:40])
            more = "" if len(preview_lines) <= 40 else f"\n  …另 {len(preview_lines) - 40} 格"
            note = f"\n(跳过 {skipped_self} 个收件人本人账号)" if skipped_self else ""
            if not messagebox.askyesno(
                    "批量邮寄 - 确认",
                    f"将真实寄给「{recipient}」共 {len(plan)} 格(整格全寄,不可逆):\n\n{preview}{more}{note}\n\n确定寄出?"):
                return
            for controller, slot, iid in plan:
                controller.action_queue.put(("mail_item", recipient, slot, iid))
            main_log(f"[界面] 已请求批量邮寄 {len(plan)} 格给 {recipient}(寄出后点「手动刷新」看背包变化)")
            dlg.destroy()

        bb = ttk.Frame(frm)
        bb.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        ttk.Button(bb, text="搜索并寄出", command=do_send).pack(side=tk.RIGHT)
        ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

    def on_tree_right_click(self, event) -> None:
        iid = self.tree.identify_row(event.y)
        if not iid:
            return
        # 右键未落在已选区时,先把选中切到该行,菜单才作用于点中的账号。
        if iid not in self.tree.selection():
            self.tree.selection_set(iid)
        try:
            self.row_menu.tk_popup(event.x_root, event.y_root)
        finally:
            self.row_menu.grab_release()

    def drain_logs(self) -> None:
        try:
            while True:
                line = self.log_queue.get_nowait()
                tag = classify_log_line(line)
                self.log_text.configure(state=self.tk.NORMAL)
                if tag:
                    self.log_text.insert(self.tk.END, line + "\n", tag)
                else:
                    self.log_text.insert(self.tk.END, line + "\n")
                self.log_text.see(self.tk.END)
                self.log_text.configure(state=self.tk.DISABLED)
        except queue.Empty:
            pass
        self.drain_statuses()
        self.refresh_farm_columns()
        self.refresh_detail_panel()
        self.root.after(100, self.drain_logs)

    @staticmethod
    def _status_tag(status_text: str) -> str:
        return {"在线": "online", "登录中": "connecting", "排队中": "connecting"}.get(status_text, "offline")

    @staticmethod
    def _cash_warn_text(farm) -> str:
        """扫 cash 背包快照,把定时表里缺货/低于阈值的物品汇成醒目提示;无则空串。
        cash 尚未同步则返回空(避免登录初期误报缺货)。GUI 线程调用,先 dict() 拷贝快照。"""
        if farm is None or not getattr(farm, "cash_observed", False):
            return ""
        schedule = parse_cash_schedule(getattr(farm.args, "cash_use_schedule", None))
        if not schedule:
            return ""
        try:
            threshold = int(getattr(farm.args, "cash_low_stock_threshold", 10))
        except (TypeError, ValueError):
            threshold = 10
        snap = dict(farm.cash_bag)
        parts: list[str] = []
        for name, _interval in schedule:
            iid = resolve_item_id(name, 0)
            if not iid:
                continue
            total = cash_count_item(snap, iid)
            if total == 0:
                parts.append(f"缺:{name}")
            elif total < threshold:
                parts.append(f"低:{name}({total})")
        return " | ".join(parts)

    def refresh_farm_columns(self) -> None:
        """把每行的 当前HP/最大HP、所在位置、Cash定时告警 刷成 controller.farm 的最新值。
        值/配色不变时跳过写入,避免每 100ms 重画整表;离线/未上线则留空。
        告警时整行套 cash_alert 红底(盖过在线配色);状态 tag 由 values[3] 反推,故本函数
        每周期兜底重算 tags,与 apply_status 的即时设置不冲突。"""
        try:
            live = auto_route.list_live_instances()
        except Exception:  # noqa: BLE001 - registry 读不动不该挡住刷新
            live = {}
        try:
            pool = socks5_pool.load_pool()
        except Exception:  # noqa: BLE001
            pool = []
        for iid in self.tree.get_children():
            controller = self.controllers.get(iid)
            farm = controller.farm if controller is not None else None
            if farm is not None and getattr(farm, "hp_seen", False):
                hp_text = f"{farm.hp:,} / {farm.max_hp:,}"
            else:
                hp_text = ""
            if farm is not None and getattr(farm, "map_seen", False):
                loc_text = resolve_map_name(farm.map_slot)
            else:
                loc_text = ""
            if farm is not None and getattr(farm, "coord_seen", False):
                coord_text = f"({farm.coord_x}, {farm.coord_y})"
            else:
                coord_text = ""
            warn_text = self._cash_warn_text(farm)
            real_char = getattr(farm, "real_character", "") if farm is not None else ""
            pid = getattr(farm, "player_id", 0) if farm is not None else 0
            uid_text = str(pid) if pid else ""
            node_text = _node_cell_text(
                getattr(controller, "args", None) if controller is not None else None,
                live, pool)
            values = list(self.tree.item(iid, "values"))
            while len(values) < 12:
                values.append("")
            status_tag = self._status_tag(values[3] if len(values) > 3 else "")
            new_tags = (status_tag, "cash_alert") if warn_text else (status_tag,)
            cur_tags = tuple(self.tree.item(iid, "tags") or ())
            changed = (values[5] != hp_text or values[6] != loc_text
                       or values[7] != coord_text
                       or values[8] != warn_text or values[9] != real_char
                       or values[10] != uid_text or values[11] != node_text)
            if changed or cur_tags != new_tags:
                values[5] = hp_text
                values[6] = loc_text
                values[7] = coord_text
                values[8] = warn_text
                values[9] = real_char
                values[10] = uid_text
                values[11] = node_text
                self.tree.item(iid, values=values, tags=new_tags)

    def on_tree_select(self, _event=None) -> None:
        self.refresh_detail_panel()

    def refresh_detail_panel(self) -> None:
        """把选中账号的会话统计汇成多行文本。累计项(钓获/用券/买券/运行时长)来自
        跨重连存活的 controller;实时项(金钱/鱼饵/Cash券余量)来自当前 farm 快照,离线显示 —。"""
        sel = self.tree.selection()
        if not sel:
            self.detail_var.set("（在上方列表选中一个账号查看统计）")
            return
        iid = sel[0]
        vals = list(self.tree.item(iid, "values"))
        who = ((vals[9] if len(vals) > 9 and vals[9] else "")
               or (vals[2] if len(vals) > 2 and vals[2] else "")
               or (vals[0] if vals else iid))
        controller = self.controllers.get(iid)
        if controller is None:
            self.detail_var.set(f"角色 {who}    （未运行，无会话统计）")
            return
        secs = max(0, int(time.monotonic() - controller.stat_start_at))
        run_hms = f"{secs // 3600:02d}:{secs % 3600 // 60:02d}:{secs % 60:02d}"
        catch = controller.stat_catch_total
        used = controller.stat_summon_count
        bought = controller.stat_summon_bought
        income = controller.stat_sell_income
        farm = controller.farm
        if farm is not None:
            gold_text = f"{farm.gold:,}" if getattr(farm, "gold_seen", False) else "—"
            bait_id = getattr(farm.args, "fishing_bait_item_id", 0)
            bait_text = f"{bag_count_item(farm, bait_id)} ({item_label(bait_id)})" if bait_id else "—"
            if getattr(farm, "cash_observed", False):
                ticket_left_text = f"{cash_count_item(dict(farm.cash_bag), SUMMON_ITEM_ID)} 张"
            else:
                ticket_left_text = "—"
        else:
            gold_text = bait_text = ticket_left_text = "—"
        self.detail_var.set(
            f"角色 {who}    运行 {run_hms}    钓获 {catch} 件\n"
            f"金钱 {gold_text}    剩余鱼饵 {bait_text}    累计卖货收入 +{income:,}\n"
            f"摊贩呼叫券:  使用 {used} 次  /  买入 {bought} 张  /  Cash 余 {ticket_left_text}"
        )

    def drain_statuses(self) -> None:
        try:
            while True:
                account, character, status = self.status_queue.get_nowait()
                self.apply_status(account, character, status)
        except queue.Empty:
            pass

    def apply_status(self, account: str, character: str, status: str) -> None:
        tag = self._status_tag(status)
        for iid in self.tree.get_children():
            values = list(self.tree.item(iid, "values"))
            if len(values) >= 3 and values[0] == account and values[2] == character:
                while len(values) < 12:
                    values.append("")
                values[3] = status
                # 仅设基础状态 tag;cash_alert 由 refresh_farm_columns 每 100ms 兜底补回。
                self.tree.item(iid, values=values, tags=(tag,))
                break

    def open_farm_settings(self) -> None:
        tk = self.tk
        ttk = self.ttk
        win = tk.Toplevel(self.root)
        win.title("挂机设置")
        win.transient(self.root)
        win.grab_set()

        defaults = self.defaults
        nb = ttk.Notebook(win)
        nb.grid(row=0, column=0, sticky="nsew")
        win.rowconfigure(0, weight=1)
        win.columnconfigure(0, weight=1)

        # 按字段前缀把 FARM_FIELD_SPECS 分流到各 tab:钓鱼+买饵一组、贩卖一组、cash 一组。
        tab_fishing = ttk.Frame(nb, padding=10)
        tab_sell = ttk.Frame(nb, padding=10)
        tab_cash = ttk.Frame(nb, padding=10)
        nb.add(tab_fishing, text="钓鱼 / 买饵")
        nb.add(tab_sell, text="贩卖")
        nb.add(tab_cash, text="Cash定时")

        def tab_for(key: str):
            if key.startswith("fishing_") or key.startswith("autobuy_"):
                return tab_fishing
            if key.startswith("autosell_"):
                return tab_sell
            return tab_cash   # cash_low_stock_threshold 及其它

        rows = {id(tab_fishing): 0, id(tab_sell): 0, id(tab_cash): 0}
        farm_vars: dict[str, object] = {}
        for key, label, kind in FARM_FIELD_SPECS:
            frame = tab_for(key)
            row = rows[id(frame)]
            rows[id(frame)] = row + 1
            current = defaults.get(key, CONFIG[key])
            if kind == "bool":
                var = tk.BooleanVar(value=_as_bool(current))
                ttk.Checkbutton(frame, text=label, variable=var).grid(
                    row=row, column=0, columnspan=2, sticky=tk.W, pady=2)
            else:
                ttk.Label(frame, text=label).grid(row=row, column=0, sticky=tk.W, pady=2)
                if kind == "itemidlist":
                    text = ",".join(item_name(v) or str(v) for v in resolve_item_id_list(current))
                elif kind == "intlist":
                    text = ",".join(str(v) for v in _coerce_int_list(current))
                elif kind == "itemid":
                    iid = resolve_item_id(current, 0)
                    text = (item_name(iid) or str(iid)) if iid else ""
                else:
                    text = str(current)
                var = tk.StringVar(value=text)
                ttk.Entry(frame, textvariable=var, width=18).grid(
                    row=row, column=1, sticky="ew", padx=(8, 0), pady=2)
            farm_vars[key] = var
        for frame in (tab_fishing, tab_sell, tab_cash):
            frame.columnconfigure(1, weight=1)

        # Cash tab 末尾追加「定时使用 cash 物品」多行编辑框(物品名=间隔秒,单值控件放不下)。
        cash_row = rows[id(tab_cash)]
        ttk.Label(tab_cash, text="定时使用Cash物品(每行一组:物品名=间隔秒)").grid(
            row=cash_row, column=0, columnspan=2, sticky=tk.W, pady=(10, 2))
        cash_text = tk.Text(tab_cash, width=34, height=6, wrap=tk.NONE)
        cash_text.grid(row=cash_row + 1, column=0, columnspan=2, sticky="ew", pady=(0, 4))
        for nm, iv in parse_cash_schedule(defaults.get("cash_use_schedule")):
            cash_text.insert(tk.END, f"{nm}={iv}\n")
        ttk.Label(tab_cash, text="例:经验药水=600(每600秒用一次);活着才用,缺货/不足阈值会在列表里红色告警").grid(
            row=cash_row + 2, column=0, columnspan=2, sticky=tk.W)

        # Cash tab 再追加「自动补货」(泡点网店买->进bank->搬到cash)。默认关闭,会花泡点。
        ttk.Separator(tab_cash, orient=tk.HORIZONTAL).grid(
            row=cash_row + 3, column=0, columnspan=2, sticky="ew", pady=(12, 6))
        restock_enabled = tk.BooleanVar(value=_as_bool(defaults.get("restock_enabled", False)))
        ttk.Checkbutton(tab_cash, text="启用自动补货(泡点购买，会花泡点)", variable=restock_enabled).grid(
            row=cash_row + 4, column=0, columnspan=2, sticky=tk.W, pady=2)
        ttk.Label(tab_cash, text="自动补货(每行一组:背包名=阈值:数量,或 背包名@网店名=阈值:数量)").grid(
            row=cash_row + 5, column=0, columnspan=2, sticky=tk.W, pady=(6, 2))
        restock_text = tk.Text(tab_cash, width=34, height=5, wrap=tk.NONE)
        restock_text.grid(row=cash_row + 6, column=0, columnspan=2, sticky="ew", pady=(0, 4))
        for bag, thr, buy, shop in parse_restock_schedule(defaults.get("restock_schedule")):
            restock_text.insert(tk.END, (f"{bag}@{shop}={thr}:{buy}\n" if shop else f"{bag}={thr}:{buy}\n"))
        ttk.Label(
            tab_cash,
            text="例:攤販呼叫券=10:50(cash里该物品总和<10就泡点买50个,进bank后自动搬到cash)。\n"
                 "商城名≠背包名时用 @ 分开:進階擺攤@進階擺攤(7天)=1:1(背包按「進階擺攤」计数,购买买网店「進階擺攤(7天)」)。\n"
                 "背包名查游戏表、网店名查网店目录。凭据=游戏账号。先用「查看背包」里的「测试补货」验证再开自动。",
            justify=tk.LEFT, foreground="#666666",
        ).grid(row=cash_row + 7, column=0, columnspan=2, sticky=tk.W)

        # Cash tab 再追加「Buff 监控」:某状态名不在就用 cash 物品(每行:状态名=物品名:间隔秒)。
        ttk.Separator(tab_cash, orient=tk.HORIZONTAL).grid(
            row=cash_row + 8, column=0, columnspan=2, sticky="ew", pady=(12, 6))
        ttk.Label(tab_cash, text="Buff监控(每行一组:状态名=物品名:间隔秒)").grid(
            row=cash_row + 9, column=0, columnspan=2, sticky=tk.W, pady=(0, 2))
        buffmon_text = tk.Text(tab_cash, width=34, height=5, wrap=tk.NONE)
        buffmon_text.grid(row=cash_row + 10, column=0, columnspan=2, sticky="ew", pady=(0, 4))
        for bn, it, iv in parse_buff_monitor_schedule(defaults.get("buff_monitor_schedule")):
            buffmon_text.insert(tk.END, f"{bn}={it}:{iv}\n")
        ttk.Label(
            tab_cash,
            text="例:進階擺攤=進階擺攤券:300(每300秒探测一次,身上没有「進階擺攤」状态就用该cash物品)。\n"
                 ":间隔秒可省,默认300(5分钟)。状态名按名匹配(可填关键词)。重连重新计时:上线后\n"
                 "第一次探测要满一个间隔才发生(默认5分钟),不会一上线就用。",
            justify=tk.LEFT, foreground="#666666",
        ).grid(row=cash_row + 11, column=0, columnspan=2, sticky=tk.W)

        # ===== 自动邮寄金钱 tab =====
        tab2 = ttk.Frame(nb, padding=12)
        nb.add(tab2, text="自动邮寄")
        mail_enabled = tk.BooleanVar(value=_as_bool(defaults.get("automail_enabled", False)))
        mail_recipient = tk.StringVar(value=str(defaults.get("automail_recipient", "") or ""))
        mail_threshold = tk.StringVar(value=str(defaults.get("automail_money_threshold", 0)))
        mail_amount = tk.StringVar(value=str(defaults.get("automail_amount", 0)))
        mail_poll = tk.StringVar(value=str(defaults.get("automail_poll_interval", 10.0)))
        mail_whitelist = tk.StringVar(value=str(defaults.get("automail_whitelist", "") or ""))
        ttk.Checkbutton(tab2, text="启用自动邮寄金钱", variable=mail_enabled).grid(
            row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
        for r, (lab, var) in enumerate((
            ("寄送给哪个角色(收件人)", mail_recipient),
            ("钱 > 此值即寄(阈值)", mail_threshold),
            ("每次寄出金额", mail_amount),
            ("循环探测间隔(秒)", mail_poll),
            ("白名单账号(逗号分隔,不寄)", mail_whitelist),
        ), start=1):
            ttk.Label(tab2, text=lab).grid(row=r, column=0, sticky=tk.W, pady=3)
            ttk.Entry(tab2, textvariable=var, width=22).grid(row=r, column=1, sticky="ew", padx=(8, 0), pady=3)
        tab2.columnconfigure(1, weight=1)
        ttk.Label(
            tab2,
            text="说明:钱包>阈值时每「循环探测间隔」寄一次给收件人;寄出后服务端(521563)刷新钱包,\n"
                 "余额回落到阈值以下即自然停寄。建议金额<阈值,收件人别填自己。\n"
                 "白名单填账号(非角色名),逗号分隔;在白名单里的账号不自动邮寄。",
            justify=tk.LEFT, foreground="#666666",
        ).grid(row=6, column=0, columnspan=2, sticky=tk.W, pady=(10, 0))

        btns = ttk.Frame(win, padding=(10, 6))
        btns.grid(row=1, column=0, columnspan=2, sticky="ew")

        def on_save() -> None:
            try:
                for skey, _slabel, skind in FARM_FIELD_SPECS:
                    raw = farm_vars[skey].get()
                    if skind == "bool":
                        defaults[skey] = bool(raw)
                    elif skind == "int":
                        defaults[skey] = parse_int_config(raw, int(CONFIG[skey]))
                    elif skind == "float":
                        defaults[skey] = float(str(raw).strip() or CONFIG[skey])
                    elif skind == "itemid":
                        iid = resolve_item_id(raw, 0)
                        defaults[skey] = (item_name(iid) or iid) if iid else 0
                    elif skind == "itemidlist":
                        defaults[skey] = [item_name(i) or i for i in resolve_item_id_list(raw)]
                    elif skind == "intlist":
                        defaults[skey] = _coerce_int_list(raw)
                cash_raw = cash_text.get("1.0", tk.END)
                sched = parse_cash_schedule(cash_raw)
                # 框里有内容却一条都没解析出来 = 格式写错(常见:误用冒号/全角)。
                # 此时保留原有配置、提示用户,绝不用空表覆盖把已有配置冲掉;留空才是真清空。
                if cash_raw.strip() and not sched:
                    main_log("[界面] 定时使用Cash物品格式无效(每行应为「物品名=间隔秒」,如 经验药水=600),已保留原有配置未改动")
                else:
                    defaults["cash_use_schedule"] = [{"item": nm, "interval": iv} for nm, iv in sched]
                bm = parse_buff_monitor_schedule(buffmon_text.get("1.0", tk.END))
                defaults["buff_monitor_schedule"] = [
                    {"buff": b, "item": i, "interval": v} for b, i, v in bm]
                defaults["automail_enabled"] = bool(mail_enabled.get())
                defaults["automail_recipient"] = str(mail_recipient.get()).strip()
                defaults["automail_money_threshold"] = parse_int_config(mail_threshold.get(), 0)
                defaults["automail_amount"] = parse_int_config(mail_amount.get(), 0)
                defaults["automail_poll_interval"] = float(str(mail_poll.get()).strip() or 10.0)
                defaults["automail_whitelist"] = ",".join(
                    a.strip() for a in str(mail_whitelist.get()).split(",") if a.strip())
                defaults["restock_enabled"] = bool(restock_enabled.get())
                rs = parse_restock_schedule(restock_text.get("1.0", tk.END))
                defaults["restock_schedule"] = [
                    ({"item": bag, "threshold": t, "buy": b, "shop": shop} if shop
                     else {"item": bag, "threshold": t, "buy": b})
                    for bag, t, b, shop in rs]
            except Exception as exc:
                main_log(f"[界面] 挂机设置保存失败：{exc}")
                return
            self.save_defaults_to_file()
            applied = self.apply_farm_settings_to_running()
            if applied:
                main_log(f"[界面] 挂机设置已保存，并热更新 {applied} 个在线账号（即时生效，无需重启）")
            else:
                main_log("[界面] 挂机设置已保存（下次启动账号生效）")
            win.destroy()

        ttk.Button(btns, text="保存", command=on_save).pack(side=tk.RIGHT)
        ttk.Button(btns, text="取消", command=win.destroy).pack(side=tk.RIGHT, padx=(0, 6))

    def _selected_controller(self) -> "AccountController | None":
        sel = self.tree.selection()
        iid = sel[0] if sel else None
        if not iid:
            return None
        return self.controllers.get(iid)

    def open_monster_view(self) -> None:
        """查看选中角色【周围怪物】实时快照(GC_CRT_LOAD/NEW/MOVE/REMOVE/DAMAGE 解析)。
        列:crtId / 怪名 / 怪种type / 阶grade / 坐标(x,y) / 距离 / 当前HP。按到玩家距离排序,自动刷新。
        怪名按 monsterType 查 monster_names.json(从游戏表 24 整表 dump),查不到显示「?」。
        HP 说明:刷怪/进图包不带 HP,只有被攻击过的怪能从 DAMAGE 包拿到当前血量,其余显示「?」。"""
        tk = self.tk
        ttk = self.ttk
        controller = self._selected_controller()
        if controller is None:
            main_log("[界面] 请先在列表里选中一个【已在线】的账号再查看周围怪物")
            return

        win = tk.Toplevel(self.root)
        win.title(f"周围怪物 - {controller.account} / {controller.character}")
        win.geometry("760x460")
        win.transient(self.root)

        header = ttk.Frame(win, padding=(10, 8))
        header.pack(fill=tk.X)
        info_var = tk.StringVar(value="")
        ttk.Label(header, textvariable=info_var).pack(side=tk.LEFT)
        auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(header, text="自动刷新(1s)", variable=auto_var).pack(side=tk.RIGHT)

        frame = ttk.Frame(win, padding=(10, 0))
        frame.pack(fill=tk.BOTH, expand=True)
        cols = ("crt", "name", "type", "grade", "pos", "dist", "hp")
        tv = ttk.Treeview(frame, columns=cols, show="headings", height=14)
        tv.heading("crt", text="怪ID")
        tv.heading("name", text="怪名")
        tv.heading("type", text="怪种")
        tv.heading("grade", text="阶")
        tv.heading("pos", text="坐标")
        tv.heading("dist", text="距离")
        tv.heading("hp", text="当前HP")
        tv.column("crt", width=80, anchor=tk.CENTER)
        tv.column("name", width=150, anchor=tk.W)
        tv.column("type", width=70, anchor=tk.CENTER)
        tv.column("grade", width=50, anchor=tk.CENTER)
        tv.column("pos", width=120, anchor=tk.CENTER)
        tv.column("dist", width=70, anchor=tk.CENTER)
        tv.column("hp", width=130, anchor=tk.CENTER)
        sb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=tv.yview)
        tv.configure(yscrollcommand=sb.set)
        tv.grid(row=0, column=0, sticky="nsew")
        sb.grid(row=0, column=1, sticky="ns")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        def refresh() -> None:
            if not win.winfo_exists():
                return
            tv.delete(*tv.get_children())
            farm = controller.farm
            if farm is None:
                info_var.set("账号未在线或尚未进入游戏,暂无怪物数据")
                return
            rows = monster_snapshot(farm)
            for e in rows:
                hp = e["hp"]
                if hp is None:
                    hp_text = "?"
                elif e["max_hp"] and e["max_hp"] != hp:
                    hp_text = f"{hp:,} / {e['max_hp']:,}"
                else:
                    hp_text = f"{hp:,}"
                dist_text = "-" if e["dist"] is None else str(e["dist"])
                nm = monster_name(e["type"]) or "?"
                tv.insert("", tk.END, values=(
                    e["crt_id"], nm, e["type"], e["grade"],
                    f"({e['x']}, {e['y']})", dist_text, hp_text,
                ))
            if not farm.monsters_observed:
                info_var.set("尚未收到怪物包(进图后服务端会下发 GC_CRT_LOAD)")
            else:
                self_pos = f" 玩家({farm.coord_x},{farm.coord_y})" if farm.coord_seen else ""
                info_var.set(f"周围怪物 {len(rows)} 只{self_pos}")

        timer = {"id": None}

        def cancel_timer() -> None:
            if timer["id"] is not None:
                try:
                    win.after_cancel(timer["id"])
                except Exception:
                    pass
                timer["id"] = None

        def tick() -> None:
            timer["id"] = None
            if not win.winfo_exists():
                return
            refresh()
            if auto_var.get():
                timer["id"] = win.after(1000, tick)

        def on_auto_toggle(*_args) -> None:
            cancel_timer()
            if auto_var.get():
                tick()

        auto_var.trace_add("write", on_auto_toggle)
        win.protocol("WM_DELETE_WINDOW", lambda: (cancel_timer(), win.destroy()))
        tick()

    def toggle_boss_monitor(self) -> None:
        """点【Boss监控】按钮切到底部的「Boss监控」页(日志/Boss监控是底部 Notebook 的两个 tab)。"""
        nb = getattr(self, "bottom_nb", None)
        tab = getattr(self, "_boss_tab", None)
        if nb is not None and tab is not None:
            nb.select(tab)

    def _build_boss_panel(self, parent) -> None:
        """把 Boss 监控控件建进给定的 tab 容器 parent。把刷新所需的控件/状态挂到 self 上,
        供 _boss_refresh / _boss_tick 读取。定时器只在切到本 tab 时跑(见 _on_bottom_tab_changed)。"""
        tk = self.tk
        ttk = self.ttk
        self._boss_panel = parent
        self._boss_watch_state = {"ids": set(), "names": []}
        self._boss_watch_state["ids"], self._boss_watch_state["names"] = \
            _parse_boss_watch(str(self.defaults.get("boss_watch", "")))

        top = ttk.Frame(parent, padding=(8, 8, 8, 0))
        top.pack(fill=tk.X)
        ttk.Label(top, text="监控名单(怪物名 或 怪种id;逗号/换行分隔;名字支持子串,如「其他次元」框住整批活动怪):").pack(anchor=tk.W)
        watch_text = tk.Text(top, height=2, wrap=tk.WORD)
        watch_text.pack(fill=tk.X, pady=(2, 0))
        watch_text.insert("1.0", str(self.defaults.get("boss_watch", "")))
        self._boss_watch_text = watch_text

        self._boss_info_var = tk.StringVar(value="")
        self._boss_auto_var = tk.BooleanVar(value=True)

        btnrow = ttk.Frame(parent, padding=(8, 0))
        btnrow.pack(fill=tk.X, pady=(6, 0))
        ttk.Button(btnrow, text="应用", command=lambda: self._boss_apply_watch(False)).pack(side=tk.LEFT)
        ttk.Button(btnrow, text="保存", command=lambda: self._boss_apply_watch(True)).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Checkbutton(btnrow, text="自动刷新(2s)", variable=self._boss_auto_var).pack(side=tk.RIGHT)
        ttk.Label(btnrow, textvariable=self._boss_info_var).pack(side=tk.LEFT, padx=(12, 0))
        self._boss_auto_var.trace_add("write", self._boss_on_auto_toggle)

        frame = ttk.Frame(parent, padding=(8, 0, 8, 8))
        frame.pack(fill=tk.BOTH, expand=True, pady=(6, 0))
        cols = ("char", "map", "name", "type", "grade", "pos", "hp", "dist")
        tv = ttk.Treeview(frame, columns=cols, show="headings", height=8)
        heads = {"char": "发现角色", "map": "地图", "name": "怪名", "type": "怪种",
                 "grade": "阶", "pos": "坐标", "hp": "当前HP", "dist": "距离"}
        wides = {"char": 120, "map": 120, "name": 150, "type": 60, "grade": 40,
                 "pos": 110, "hp": 130, "dist": 60}
        for c in cols:
            tv.heading(c, text=heads[c])
            tv.column(c, width=wides[c], anchor=(tk.W if c in ("char", "map", "name") else tk.CENTER))
        tv.tag_configure("hit", background="#fff3cd")
        sb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=tv.yview)
        tv.configure(yscrollcommand=sb.set)
        tv.grid(row=0, column=0, sticky="nsew")
        sb.grid(row=0, column=1, sticky="ns")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)
        self._boss_tv = tv

    def _sync_boss_watch_ui(self) -> None:
        """defaults 加载/重载后,把 boss_watch 刷进监控名单编辑框与匹配状态。
        (build_ui 建面板时 self.defaults 还是空 dict,必须等 defaults 到位后再灌一次。)"""
        text = str(self.defaults.get("boss_watch", ""))
        if hasattr(self, "_boss_watch_state"):
            self._boss_watch_state["ids"], self._boss_watch_state["names"] = _parse_boss_watch(text)
        wt = getattr(self, "_boss_watch_text", None)
        if wt is not None:
            wt.delete("1.0", "end")
            if text:
                wt.insert("1.0", text)

    def _boss_apply_watch(self, save: bool) -> None:
        raw = self._boss_watch_text.get("1.0", "end").strip()
        self._boss_watch_state["ids"], self._boss_watch_state["names"] = _parse_boss_watch(raw)
        if save:
            self.defaults["boss_watch"] = raw
            try:
                self.save_defaults_to_file()
            except Exception:
                main_log("[界面] 保存 Boss 监控名单失败")
        self._boss_refresh()

    def _boss_refresh(self) -> None:
        panel = getattr(self, "_boss_panel", None)
        if panel is None or not panel.winfo_exists():
            return
        tk = self.tk
        tv = self._boss_tv
        tv.delete(*tv.get_children())
        ids, names = self._boss_watch_state["ids"], self._boss_watch_state["names"]
        if not ids and not names:
            self._boss_info_var.set("名单为空:上面填要监控的 Boss 名/怪种id,点【应用】")
            return
        online = 0
        rows: list[tuple] = []
        for ctrl in list(self.controllers.values()):
            farm = ctrl.farm
            if farm is None:
                continue
            online += 1
            map_text = resolve_map_name(farm.map_slot) if getattr(farm, "map_seen", False) else "?"
            for e in monster_snapshot(farm):
                nm = monster_name(e["type"]) or ""
                if not _boss_watch_match(e["type"], nm, ids, names):
                    continue
                hp = e["hp"]
                if hp is None:
                    hp_text = "?"
                elif e["max_hp"] and e["max_hp"] != hp:
                    hp_text = f"{hp:,} / {e['max_hp']:,}"
                else:
                    hp_text = f"{hp:,}"
                dist = e["dist"]
                rows.append((
                    dist if dist is not None else 1 << 30,
                    (ctrl.character or ctrl.account, map_text, nm or "?", e["type"],
                     e["grade"], f"({e['x']}, {e['y']})", hp_text,
                     "-" if dist is None else str(dist)),
                ))
        rows.sort(key=lambda r: r[0])
        for _d, vals in rows:
            tv.insert("", tk.END, values=vals, tags=("hit",))
        self._boss_info_var.set(f"在线角色 {online} · 命中 {len(rows)} 只")

    def _boss_cancel_timer(self) -> None:
        tid = getattr(self, "_boss_timer_id", None)
        if tid is not None:
            try:
                self.root.after_cancel(tid)
            except Exception:
                pass
        self._boss_timer_id = None

    def _boss_tick(self) -> None:
        self._boss_timer_id = None
        panel = getattr(self, "_boss_panel", None)
        if panel is None or not panel.winfo_ismapped():
            return
        self._boss_refresh()
        if self._boss_auto_var.get():
            self._boss_timer_id = self.root.after(2000, self._boss_tick)

    def _boss_on_auto_toggle(self, *_args) -> None:
        self._boss_cancel_timer()
        if self._boss_auto_var.get():
            self._boss_tick()

    def _on_bottom_tab_changed(self, _evt=None) -> None:
        """底部 Notebook 切页:切到 Boss 监控页就启动刷新,切回日志就停掉定时器。"""
        nb = getattr(self, "bottom_nb", None)
        tab = getattr(self, "_boss_tab", None)
        if nb is None or tab is None:
            return
        try:
            on_boss = nb.select() == str(tab)
        except Exception:
            on_boss = False
        if on_boss:
            self._boss_tick()
        else:
            self._boss_cancel_timer()

    def open_cashbuff_view(self) -> None:
        """查看选中角色的 cash 状态 buff(511337 加 / 511348 移除 解析出的快照)。
        显示 type / 名称 / 数值 / 到期时间 / 剩余,自动刷新。名称按 type 硬编码表
        (CASH_BUFF_TYPE_NAMES,包里只带 type 不带名)。"""
        tk = self.tk
        ttk = self.ttk
        controller = self._selected_controller()
        if controller is None:
            main_log("[界面] 请先在列表里选中一个【已在线】的账号再查看cashbuff")
            return

        win = tk.Toplevel(self.root)
        win.title(f"Cash状态 - {controller.account} / {controller.character}")
        win.geometry("560x420")
        win.transient(self.root)

        header = ttk.Frame(win, padding=(10, 8))
        header.pack(fill=tk.X)
        info_var = tk.StringVar(value="")
        ttk.Label(header, textvariable=info_var).pack(side=tk.LEFT)
        auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(header, text="自动刷新(1s)", variable=auto_var).pack(side=tk.RIGHT)

        frame = ttk.Frame(win, padding=(10, 0))
        frame.pack(fill=tk.BOTH, expand=True)
        cols = ("type", "name", "value", "expiry", "left")
        tv = ttk.Treeview(frame, columns=cols, show="headings", height=12)
        tv.heading("type", text="type")
        tv.heading("name", text="状态名")
        tv.heading("value", text="数值")
        tv.heading("expiry", text="到期时间")
        tv.heading("left", text="剩余")
        tv.column("type", width=50, anchor=tk.CENTER)
        tv.column("name", width=170, anchor=tk.W)
        tv.column("value", width=60, anchor=tk.CENTER)
        tv.column("expiry", width=150, anchor=tk.CENTER)
        tv.column("left", width=90, anchor=tk.CENTER)
        sb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=tv.yview)
        tv.configure(yscrollcommand=sb.set)
        tv.grid(row=0, column=0, sticky="nsew")
        sb.grid(row=0, column=1, sticky="ns")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        def fmt_expiry(expiry: int) -> tuple[str, str]:
            # expiry = 绝对 unix 秒。返回 (到期时间字符串, 剩余字符串)。
            if not expiry:
                return ("常驻/未知", "-")
            when = time.strftime("%Y-%m-%d %H:%M", time.localtime(expiry))
            left = int(expiry - time.time())
            if left <= 0:
                return (when, "已过期")
            d, rem = divmod(left, 86400)
            h, rem = divmod(rem, 3600)
            m, _ = divmod(rem, 60)
            left_s = (f"{d}天" if d else "") + (f"{h}时" if (d or h) else "") + f"{m}分"
            return (when, left_s)

        def refresh() -> None:
            if not win.winfo_exists():
                return
            tv.delete(*tv.get_children())
            farm = controller.farm
            if farm is None:
                info_var.set("账号未在线或尚未进入游戏,暂无状态数据")
                return
            cash = sorted(farm.cash_buffs.values(), key=lambda b: b["type"])
            for b in cash:
                when, left = fmt_expiry(b.get("expiry", 0))
                tv.insert("", tk.END, values=(b["type"], b["name"], b["value"], when, left))
            # 进阶/技能型 cash buff(disc!=0,如进阶摆摊):type 列加 # 前缀标明是 buffId,
            # 跟上面的 cash type 区分(两者命名空间不同)。
            skill = sorted(farm.skill_buffs.values(), key=lambda b: b["type"])
            for b in skill:
                when, left = fmt_expiry(b.get("expiry", 0))
                tv.insert("", tk.END, values=(f"#{b['type']}", b["name"], b["value"], when, left))
            if not farm.cash_buffs_observed:
                info_var.set("尚未收到状态包(进游戏后服务端会下发 511337)")
            else:
                extra = f"(含 {len(skill)} 个进阶/技能型)" if skill else ""
                info_var.set(f"共 {len(cash) + len(skill)} 个 cash 状态{extra}")

        timer = {"id": None}

        def cancel_timer() -> None:
            if timer["id"] is not None:
                try:
                    win.after_cancel(timer["id"])
                except Exception:
                    pass
                timer["id"] = None

        def tick() -> None:
            timer["id"] = None
            if not win.winfo_exists():
                return
            refresh()
            if auto_var.get():
                timer["id"] = win.after(1000, tick)

        def on_auto_toggle(*_args) -> None:
            cancel_timer()
            if auto_var.get():
                tick()

        auto_var.trace_add("write", on_auto_toggle)
        win.protocol("WM_DELETE_WINDOW", lambda: (cancel_timer(), win.destroy()))
        tick()

    def open_bag_view(self) -> None:
        tk = self.tk
        ttk = self.ttk
        controller = self._selected_controller()
        if controller is None:
            main_log("[界面] 请先在列表里选中一个【已在线】的账号再查看背包")
            return

        win = tk.Toplevel(self.root)
        win.title(f"背包查看 - {controller.account} / {controller.character}")
        win.geometry("680x560")
        win.transient(self.root)

        header = ttk.Frame(win, padding=(10, 8))
        header.pack(fill=tk.X)
        gold_var = tk.StringVar(value="金币 未知")
        ttk.Label(header, textvariable=gold_var, font=("", 10, "bold")).pack(side=tk.LEFT)
        auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(header, text="自动刷新(1s)", variable=auto_var).pack(side=tk.RIGHT)
        info_var = tk.StringVar(value="")
        ttk.Label(header, textvariable=info_var).pack(side=tk.RIGHT, padx=(0, 12))

        # 按钮栏:必须早于两个表格 pack、且 side=BOTTOM 钉到底部,否则表格 expand=True
        # 会占满 680x560,把最后 pack 的按钮栏挤出可视区(之前"看不到卖货按钮"的原因)。
        btns = ttk.Frame(win, padding=(10, 6))
        btns.pack(side=tk.BOTTOM, fill=tk.X)

        def make_table(parent, title: str):
            frame = ttk.LabelFrame(parent, text=title)
            frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 8))
            cols = ("slot", "item_id", "name", "count")
            tv = ttk.Treeview(frame, columns=cols, show="headings", height=10)
            tv.heading("slot", text="格子")
            tv.heading("item_id", text="物品ID")
            tv.heading("name", text="物品名")
            tv.heading("count", text="数量")
            tv.column("slot", width=60, anchor=tk.CENTER)
            tv.column("item_id", width=90, anchor=tk.CENTER)
            tv.column("name", width=170, anchor=tk.W)
            tv.column("count", width=90, anchor=tk.CENTER)
            sb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=tv.yview)
            tv.configure(yscrollcommand=sb.set)
            tv.grid(row=0, column=0, sticky="nsew")
            sb.grid(row=0, column=1, sticky="ns")
            frame.columnconfigure(0, weight=1)
            frame.rowconfigure(0, weight=1)
            return tv

        bag_tv = make_table(win, "背包(511591 整桶快照)")
        cash_tv = make_table(win, "Cash 背包(511324 整桶快照)")

        def fill(tv, snapshot: dict) -> int:
            tv.delete(*tv.get_children())
            shown = 0
            for slot in sorted(snapshot):
                iid_, cnt = snapshot[slot]
                if not iid_ and not cnt:
                    continue
                tv.insert("", tk.END, values=(slot, iid_, item_name(iid_) or "", cnt))
                shown += 1
            return shown

        def refresh() -> None:
            if not win.winfo_exists():
                return
            farm = controller.farm
            if farm is None:
                gold_var.set("金币 未知")
                info_var.set("账号未在线或尚未进入游戏,暂无背包数据")
                bag_tv.delete(*bag_tv.get_children())
                cash_tv.delete(*cash_tv.get_children())
            else:
                gold_var.set(f"金币 {farm.gold:,}" if farm.gold_seen else "金币 未知(尚未回流)")
                bag_snap = dict(farm.bag)
                cash_snap = dict(farm.cash_bag)
                nb = fill(bag_tv, bag_snap)
                nc = fill(cash_tv, cash_snap)
                info_var.set(f"背包 {nb} 项 / Cash {nc} 项"
                             + ("" if farm.cash_observed else "(Cash 尚未同步)"))

        timer = {"id": None}

        def cancel_timer() -> None:
            if timer["id"] is not None:
                try:
                    win.after_cancel(timer["id"])
                except Exception:
                    pass
                timer["id"] = None

        def tick() -> None:
            timer["id"] = None
            if not win.winfo_exists():
                return
            refresh()
            if auto_var.get():
                timer["id"] = win.after(1000, tick)

        def on_auto_toggle(*_args) -> None:
            cancel_timer()
            if auto_var.get():
                tick()

        auto_var.trace_add("write", on_auto_toggle)
        win.protocol("WM_DELETE_WINDOW", lambda: (cancel_timer(), win.destroy()))

        from tkinter import messagebox

        def _confirm_sell() -> None:
            # 手动卖货:按该账号配置的贩卖范围,确认框里先列出将卖出的具体格子(不可逆)。
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法卖货")
                return
            fargs = farm.args
            keep = set(getattr(fargs, "autosell_keep_item_ids", []) or [])
            lo = max(0, getattr(fargs, "autosell_start_slot", 0))
            hi = min(BAG_MAIN_SLOTS - 1, getattr(fargs, "autosell_end_slot", BAG_MAIN_SLOTS - 1))
            bag = dict(farm.bag)
            sell_list = [(s, bag[s][0], bag[s][1]) for s in range(lo, hi + 1)
                         if bag.get(s, (0, 0))[0] and bag[s][1] > 0 and bag[s][0] not in keep]
            if not sell_list:
                messagebox.showinfo("立即卖货",
                                    f"贩卖范围[{lo}-{hi}]内没有可卖物品(已跳过保留ID {sorted(keep)})")
                return
            preview = "\n".join(f"  格{s} {item_label(iid)} x{cnt}" for s, iid, cnt in sell_list[:12])
            more = "" if len(sell_list) <= 12 else f"\n  ...另外 {len(sell_list) - 12} 格"
            msg = (f"将真实卖出以下 {len(sell_list)} 格(范围[{lo}-{hi}],跳过保留ID {sorted(keep)}):\n"
                   f"{preview}{more}\n\n此操作不可逆!确定卖出?")
            if messagebox.askyesno("立即卖货 - 确认", msg):
                controller.action_queue.put("sell")
                main_log(f"[界面] 已请求立即卖货({len(sell_list)}格),执行后点「手动刷新」看背包变化")

        def _confirm_buy() -> None:
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法买鱼饵")
                return
            fargs = farm.args
            bait_id = getattr(fargs, "autobuy_item_id", 0) or getattr(fargs, "fishing_bait_item_id", 0)
            if not bait_id:
                messagebox.showwarning("立即买鱼饵",
                                       "该账号未配置要购买的 itemId(autobuy_item_id),请先在「挂机设置」里填写")
                return
            summon = find_summon_slot(farm)
            if summon is None:
                hint = ("cash 背包尚未同步(511324)" if not farm.cash_observed
                        else f"cash 背包内找不到摊贩呼叫卷({item_label(SUMMON_ITEM_ID)})")
                messagebox.showwarning("立即买鱼饵", f"无法召唤摊贩:{hint}")
                return
            # 弹框让用户输入数量(默认取配置的 autobuy_count),走和自动同一逻辑(autobuy_run)。
            dlg = tk.Toplevel(win)
            dlg.title("立即买鱼饵")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            qv = tk.StringVar(value=str(getattr(fargs, "autobuy_count", 0) or 300))
            ttk.Label(frm, text=f"物品:{item_label(bait_id)}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 4))
            ttk.Label(frm, text=f"现有:{bag_count_item(farm, bait_id)}  摊贩呼叫卷:cash格{summon}").grid(
                row=1, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            ttk.Label(frm, text="购买数量").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=qv, width=16).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            frm.columnconfigure(1, weight=1)

            def do_buy() -> None:
                try:
                    qty = int(str(qv.get()).strip())
                except ValueError:
                    messagebox.showwarning("立即买鱼饵", "数量必须是整数")
                    return
                if qty <= 0:
                    messagebox.showwarning("立即买鱼饵", "数量必须 > 0")
                    return
                controller.action_queue.put(("buy", qty))
                main_log(f"[界面] 已请求立即买鱼饵 {item_label(bait_id)} x{qty},执行后点「手动刷新」看背包变化")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="购买", command=do_buy).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        def _sell_one_slot(slot: int, iid: int, cnt: int) -> None:
            # 右键单格贩卖:用户显式指定该格,不走 keep/sellable 过滤(鱼饵也能卖)。
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法卖货")
                return
            if messagebox.askyesno(
                    "右键卖出 - 确认",
                    f"将真实卖出单格:\n  格{slot} {item_label(iid)} x{cnt}\n\n此操作不可逆!确定卖出?"):
                controller.action_queue.put(("sell_slot", slot))
                main_log(f"[界面] 已请求卖出单格{slot}({item_label(iid)} x{cnt}),执行后点「手动刷新」看变化")

        def _test_mail() -> None:
            # 手动测试自动邮寄:弹框填收件人/金额(默认取该账号配置),确认后走 action_queue
            # 在 worker 线程发 411524。寄出后服务端回 521563 刷新钱包,点「手动刷新」即可看到金币减少。
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法邮寄")
                return
            fargs = farm.args
            gold_now = farm.gold if farm.gold_seen else None
            dlg = tk.Toplevel(win)
            dlg.title("测试邮寄金钱")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            rv = tk.StringVar(value=str(getattr(fargs, "automail_recipient", "") or ""))
            av = tk.StringVar(value=str(getattr(fargs, "automail_amount", 0) or ""))
            ttk.Label(frm, text=f"寄件人:{controller.character}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            ttk.Label(frm, text="收件人角色名").grid(row=1, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=rv, width=20).grid(row=1, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="金额").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=av, width=20).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text=(f"当前钱包:{gold_now:,}" if gold_now is not None else "当前钱包:未知(尚未回流)")).grid(
                row=3, column=0, columnspan=2, sticky=tk.W, pady=(6, 0))
            frm.columnconfigure(1, weight=1)

            def do_send() -> None:
                recipient = rv.get().strip()
                try:
                    amount = int(str(av.get()).strip())
                except ValueError:
                    messagebox.showwarning("测试邮寄", "金额必须是整数")
                    return
                if not recipient or amount <= 0:
                    messagebox.showwarning("测试邮寄", "收件人不能为空,金额必须 > 0")
                    return
                if recipient == controller.character and not messagebox.askyesno(
                        "测试邮寄", "收件人就是自己,确定继续?"):
                    return
                if gold_now is not None and amount > gold_now:
                    messagebox.showwarning("测试邮寄", f"金额 {amount:,} 超过当前钱包 {gold_now:,}")
                    return
                controller.action_queue.put(("mail", recipient, amount))
                main_log(f"[界面] 已请求测试邮寄 {amount:,} 给 {recipient}(寄出后点「手动刷新」看钱包变化)")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="发送", command=do_send).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        def _test_restock() -> None:
            # 手动测试自动补货:填网店itemID+买几个,确认后走 action_queue 在 worker 线程
            # 跑完整流程(泡点购买->开bank->搬到cash)。会真的花泡点!
            controller = self._selected_controller()
            if controller is None or controller.farm is None:
                main_log("[界面] 账号未在线,无法补货")
                return
            dlg = tk.Toplevel(win)
            dlg.title("测试补货(泡点购买)")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            iv = tk.StringVar(value="")
            cv = tk.StringVar(value="1")
            ttk.Label(frm, text=f"角色:{controller.character}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            ttk.Label(frm, text="物品名(或ID)").grid(row=1, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=iv, width=20).grid(row=1, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="购买数量").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=cv, width=20).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="会真的花泡点!买完自动开bank并搬到cash,看日志/手动刷新。",
                      foreground="#b00020").grid(row=3, column=0, columnspan=2, sticky=tk.W, pady=(6, 0))
            frm.columnconfigure(1, weight=1)

            def do_buy() -> None:
                text = str(iv.get()).strip()
                # 先查游戏表(本地,无网络);查不到再退网店目录(覆盖「進階擺攤(7天)」这种网店专属名,会拉一次/api)
                item_id = resolve_item_id(text, 0, warn=False) or resolve_shop_id(text, 0)
                try:
                    count = int(str(cv.get()).strip())
                except ValueError:
                    messagebox.showwarning("测试补货", "数量必须是整数")
                    return
                if not item_id:
                    messagebox.showwarning("测试补货", "物品名/ID 解析不到(游戏表和网店目录都没有)")
                    return
                if count <= 0:
                    messagebox.showwarning("测试补货", "数量必须 > 0")
                    return
                if not messagebox.askyesno("测试补货", f"将用泡点购买 {item_label(item_id)} x{count} 并搬到cash。\n会真的花泡点,确定?"):
                    return
                controller.action_queue.put(("restock", item_id, count))
                main_log(f"[界面] 已请求测试补货 {item_label(item_id)} x{count}(看日志,完成后手动刷新看cash)")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="购买", command=do_buy).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        bag_menu = tk.Menu(win, tearoff=0)

        def _bag_rclick(event) -> None:
            row = bag_tv.identify_row(event.y)
            if not row:
                return
            bag_tv.selection_set(row)
            vals = bag_tv.item(row, "values")  # (slot, item_id, name, count)
            try:
                slot, iid, cnt = int(vals[0]), int(vals[1]), int(vals[3])
            except (ValueError, IndexError):
                return
            bag_menu.delete(0, tk.END)
            bag_menu.add_command(label=f"卖出此格:{item_name(iid) or iid} x{cnt}",
                                 command=lambda: _sell_one_slot(slot, iid, cnt))
            bag_menu.tk_popup(event.x_root, event.y_root)

        bag_tv.bind("<Button-3>", _bag_rclick)

        ttk.Button(btns, text="整体卖货(范围)", command=_confirm_sell).pack(side=tk.LEFT)
        ttk.Button(btns, text="立即买鱼饵", command=_confirm_buy).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="测试邮寄", command=_test_mail).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="测试补货", command=_test_restock).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Label(btns, text="提示:背包格上右键可单格贩卖").pack(side=tk.LEFT, padx=(12, 0))
        ttk.Button(btns, text="手动刷新", command=refresh).pack(side=tk.RIGHT)
        ttk.Button(btns, text="关闭",
                   command=lambda: (cancel_timer(), win.destroy())).pack(side=tk.RIGHT, padx=(0, 6))

        tick()

    def stop_stall_selected(self) -> None:
        controller = self._selected_controller()
        if controller is None or controller.farm is None:
            main_log("[界面] 请先选中一个【已在线】的账号再停止摆摊")
            return
        controller.action_queue.put("stall_close")
        main_log(f"[界面] 已请求停止摆摊({controller.account}),发送 CG_STREETSTALL_TERMINATE 412050")

    def open_stall_view(self) -> None:
        """个人摆摊面板:从背包/cash 挑物品(数量+单价)上架,发真实摆摊包(411040/411049),
        511110 回报的剩余量在「当前摊位剩余」实时刷新,「停止摆摊」发 412050。"""
        tk = self.tk
        ttk = self.ttk
        from tkinter import messagebox
        controller = self._selected_controller()
        if controller is None:
            main_log("[界面] 请先在列表里选中一个【已在线】的账号再摆摊")
            return

        win = tk.Toplevel(self.root)
        win.title(f"摆摊 - {controller.account} / {controller.character}")
        win.geometry("820x860")
        win.transient(self.root)

        # 待上架列表(python 侧):每项 {bag_type, src_slot, item_id, name, qty, price}。
        # 确认时打包成 listings 投递 action_queue,在 worker 线程发包。
        pending: list[dict] = []

        header = ttk.Frame(win, padding=(10, 8))
        header.pack(fill=tk.X)
        ttk.Label(header, text="摊位名").pack(side=tk.LEFT)
        default_name = str(
            getattr(controller.args, "stall_default_name", "") if controller.args else ""
        ) or str(CONFIG.get("stall_default_name", "小店"))
        name_var = tk.StringVar(value=default_name)
        ttk.Entry(header, textvariable=name_var, width=18).pack(side=tk.LEFT, padx=(4, 12))
        auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(header, text="自动刷新(1s)", variable=auto_var).pack(side=tk.RIGHT)
        status_var = tk.StringVar(value="")
        ttk.Label(header, textvariable=status_var, font=("", 10, "bold")).pack(side=tk.RIGHT, padx=(0, 12))

        # 底部按钮栏先 pack 钉底,避免被上方 expand 的表格挤出可视区(同 open_bag_view 的处理)。
        btns = ttk.Frame(win, padding=(10, 6))
        btns.pack(side=tk.BOTTOM, fill=tk.X)

        status_frame = ttk.LabelFrame(win, text="当前摊位剩余(511110 实时回报)")
        status_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=10, pady=(0, 6))
        stall_text = tk.Text(status_frame, height=6, wrap=tk.NONE)
        stall_text.configure(state=tk.DISABLED)
        stall_text.pack(fill=tk.X, padx=6, pady=6)

        def make_src_table(title: str):
            frame = ttk.LabelFrame(win, text=title)
            frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 6))
            cols = ("slot", "item_id", "name", "count")
            tv = ttk.Treeview(frame, columns=cols, show="headings", height=6)
            tv.heading("slot", text="格子")
            tv.heading("item_id", text="物品ID")
            tv.heading("name", text="物品名")
            tv.heading("count", text="数量")
            tv.column("slot", width=60, anchor=tk.CENTER)
            tv.column("item_id", width=90, anchor=tk.CENTER)
            tv.column("name", width=180, anchor=tk.W)
            tv.column("count", width=80, anchor=tk.CENTER)
            sb = ttk.Scrollbar(frame, orient=tk.VERTICAL, command=tv.yview)
            tv.configure(yscrollcommand=sb.set)
            tv.grid(row=0, column=0, sticky="nsew")
            sb.grid(row=0, column=1, sticky="ns")
            frame.columnconfigure(0, weight=1)
            frame.rowconfigure(0, weight=1)
            return tv

        bag_tv = make_src_table("背包(右键此处物品 → 添加上架)")
        cash_tv = make_src_table("Cash 背包(右键此处物品 → 添加上架)")

        # 银行/账号共享仓库:按模板聚合(件数=总数量,占用格=占用银行格数),右键把整批拖进 cash。
        bank_frame = ttk.LabelFrame(win, text="银行/仓库(右键 → 拖到cash背包;先点「刷新银行」拉取内容)")
        bank_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 6))
        bank_cols = ("item_id", "name", "count", "slots")
        bank_tv = ttk.Treeview(bank_frame, columns=bank_cols, show="headings", height=6)
        bank_tv.heading("item_id", text="物品ID")
        bank_tv.heading("name", text="物品名")
        bank_tv.heading("count", text="件数")
        bank_tv.heading("slots", text="占用格")
        bank_tv.column("item_id", width=90, anchor=tk.CENTER)
        bank_tv.column("name", width=200, anchor=tk.W)
        bank_tv.column("count", width=80, anchor=tk.CENTER)
        bank_tv.column("slots", width=80, anchor=tk.CENTER)
        bank_sb = ttk.Scrollbar(bank_frame, orient=tk.VERTICAL, command=bank_tv.yview)
        bank_tv.configure(yscrollcommand=bank_sb.set)
        bank_tv.grid(row=0, column=0, sticky="nsew")
        bank_sb.grid(row=0, column=1, sticky="ns")
        bank_frame.columnconfigure(0, weight=1)
        bank_frame.rowconfigure(0, weight=1)

        sel_frame = ttk.LabelFrame(win, text=f"待上架(最多 {STALL_MAX_SLOTS} 件,右键移除)")
        sel_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 6))
        sel_cols = ("src", "slot", "item", "qty", "price")
        sel_tv = ttk.Treeview(sel_frame, columns=sel_cols, show="headings", height=6)
        sel_tv.heading("src", text="来源")
        sel_tv.heading("slot", text="格子")
        sel_tv.heading("item", text="物品")
        sel_tv.heading("qty", text="数量")
        sel_tv.heading("price", text="单价")
        sel_tv.column("src", width=70, anchor=tk.CENTER)
        sel_tv.column("slot", width=60, anchor=tk.CENTER)
        sel_tv.column("item", width=220, anchor=tk.W)
        sel_tv.column("qty", width=80, anchor=tk.CENTER)
        sel_tv.column("price", width=100, anchor=tk.CENTER)
        ssb = ttk.Scrollbar(sel_frame, orient=tk.VERTICAL, command=sel_tv.yview)
        sel_tv.configure(yscrollcommand=ssb.set)
        sel_tv.grid(row=0, column=0, sticky="nsew")
        ssb.grid(row=0, column=1, sticky="ns")
        sel_frame.columnconfigure(0, weight=1)
        sel_frame.rowconfigure(0, weight=1)

        def src_label(bag_type: int) -> str:
            return "cash" if bag_type == STALL_BAG_CASH else "背包"

        def refill_sel() -> None:
            sel_tv.delete(*sel_tv.get_children())
            for p in pending:
                sel_tv.insert("", tk.END, values=(
                    src_label(p["bag_type"]), p["src_slot"],
                    item_label(p["item_id"]), p["qty"], p["price"]))

        def add_listing(bag_type: int, slot: int, iid: int, have: int) -> None:
            if len(pending) >= STALL_MAX_SLOTS:
                messagebox.showwarning("摆摊", f"待上架已满 {STALL_MAX_SLOTS} 件,先移除一些")
                return
            if any(p["bag_type"] == bag_type and p["src_slot"] == slot for p in pending):
                messagebox.showinfo("摆摊", f"{src_label(bag_type)}格{slot} 已在待上架列表里")
                return
            dlg = tk.Toplevel(win)
            dlg.title("添加上架物品")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            ttk.Label(frm, text=f"物品:{item_label(iid)}").grid(row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 4))
            ttk.Label(frm, text=f"来源:{src_label(bag_type)}格{slot}  现有 {have}").grid(
                row=1, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            qv = tk.StringVar(value=str(have))
            pv = tk.StringVar(value="0")
            ttk.Label(frm, text="上架数量").grid(row=2, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=qv, width=16).grid(row=2, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="单价(金币)").grid(row=3, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=pv, width=16).grid(row=3, column=1, sticky="ew", padx=(8, 0))
            frm.columnconfigure(1, weight=1)

            def do_add() -> None:
                try:
                    qty = int(str(qv.get()).strip())
                    price = int(str(pv.get()).strip())
                except ValueError:
                    messagebox.showwarning("摆摊", "数量和单价必须是整数")
                    return
                if qty <= 0:
                    messagebox.showwarning("摆摊", "数量必须 > 0")
                    return
                if qty > have:
                    messagebox.showwarning("摆摊", f"数量 {qty} 超过现有 {have}")
                    return
                if price < 0:
                    messagebox.showwarning("摆摊", "单价不能为负")
                    return
                pending.append({"bag_type": bag_type, "src_slot": slot,
                                "item_id": iid, "qty": qty, "price": price})
                refill_sel()
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=4, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="添加", command=do_add).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        def make_src_rclick(tv, bag_type: int):
            menu = tk.Menu(win, tearoff=0)

            def handler(event) -> None:
                row = tv.identify_row(event.y)
                if not row:
                    return
                tv.selection_set(row)
                vals = tv.item(row, "values")  # (slot, item_id, name, count)
                try:
                    slot, iid, cnt = int(vals[0]), int(vals[1]), int(vals[3])
                except (ValueError, IndexError):
                    return
                menu.delete(0, tk.END)
                menu.add_command(label=f"添加上架:{item_name(iid) or iid} x{cnt}",
                                 command=lambda: add_listing(bag_type, slot, iid, cnt))
                menu.tk_popup(event.x_root, event.y_root)
            return handler

        bag_tv.bind("<Button-3>", make_src_rclick(bag_tv, STALL_BAG_NORMAL))
        cash_tv.bind("<Button-3>", make_src_rclick(cash_tv, STALL_BAG_CASH))

        def bank_pull(template_id: int, count: int) -> None:
            # count<=0 = 该物品全部实例;worker 端逐 uid 搬,不可堆叠物品每件落独立 cash 空格,cash 满即停。
            if controller.farm is None:
                main_log("[界面] 账号未在线,无法从银行拖物品")
                return
            controller.action_queue.put(("bank_pull", int(template_id), int(count)))
            main_log(f"[界面] 已请求把银行 {item_label(template_id)} "
                     f"{'全部' if count <= 0 else count} 拖到cash(看日志/面板刷新)")

        def bank_pull_n(template_id: int, have: int) -> None:
            dlg = tk.Toplevel(win)
            dlg.title("拖出指定数量")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)
            ttk.Label(frm, text=f"物品:{item_label(template_id)}  银行现有 {have}").grid(
                row=0, column=0, columnspan=2, sticky=tk.W, pady=(0, 6))
            qv = tk.StringVar(value=str(have))
            ttk.Label(frm, text="拖出数量").grid(row=1, column=0, sticky=tk.W, pady=3)
            ttk.Entry(frm, textvariable=qv, width=16).grid(row=1, column=1, sticky="ew", padx=(8, 0))
            frm.columnconfigure(1, weight=1)

            def do_pull() -> None:
                try:
                    n = int(str(qv.get()).strip())
                except ValueError:
                    messagebox.showwarning("拖出", "数量必须是整数")
                    return
                if n <= 0:
                    messagebox.showwarning("拖出", "数量必须 > 0")
                    return
                bank_pull(template_id, n)
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="拖出", command=do_pull).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))

        bank_menu = tk.Menu(win, tearoff=0)

        def bank_rclick(event) -> None:
            row = bank_tv.identify_row(event.y)
            if not row:
                return
            bank_tv.selection_set(row)
            vals = bank_tv.item(row, "values")  # (item_id, name, count, slots)
            try:
                tmpl, total = int(vals[0]), int(vals[2])
            except (ValueError, IndexError):
                return
            bank_menu.delete(0, tk.END)
            bank_menu.add_command(label=f"全部拖到cash背包(x{total})",
                                  command=lambda: bank_pull(tmpl, 0))
            bank_menu.add_command(label="拖出指定数量…",
                                  command=lambda: bank_pull_n(tmpl, total))
            bank_menu.tk_popup(event.x_root, event.y_root)

        bank_tv.bind("<Button-3>", bank_rclick)

        sel_menu = tk.Menu(win, tearoff=0)

        def sel_rclick(event) -> None:
            row = sel_tv.identify_row(event.y)
            if not row:
                return
            idx = sel_tv.index(row)
            sel_tv.selection_set(row)
            sel_menu.delete(0, tk.END)
            sel_menu.add_command(label="从待上架移除",
                                 command=lambda: (pending.pop(idx), refill_sel())
                                 if 0 <= idx < len(pending) else None)
            sel_menu.tk_popup(event.x_root, event.y_root)

        sel_tv.bind("<Button-3>", sel_rclick)

        def fill_src(tv, snapshot: dict) -> None:
            tv.delete(*tv.get_children())
            for slot in sorted(snapshot):
                iid_, cnt = snapshot[slot]
                if not iid_ or cnt <= 0:
                    continue
                tv.insert("", tk.END, values=(slot, iid_, item_name(iid_) or "", cnt))

        def fill_bank(tv, bank: dict) -> None:
            # bank: uid -> (模板itemId, count)。按模板聚合成 [总件数, 占用银行格数];
            # 不可堆叠物品(如寶石箱)一格一 uid,占用格=uid 数,件数=总数量,右键可整批拖出。
            tv.delete(*tv.get_children())
            agg: dict[int, list[int]] = {}
            for _uid, (tmpl, cnt) in bank.items():
                if not tmpl or cnt <= 0:
                    continue
                e = agg.setdefault(tmpl, [0, 0])
                e[0] += cnt
                e[1] += 1
            for tmpl in sorted(agg, key=lambda t: item_name(t) or str(t)):
                total, slots = agg[tmpl]
                tv.insert("", tk.END, values=(tmpl, item_name(tmpl) or "", total, slots))

        def refresh() -> None:
            if not win.winfo_exists():
                return
            farm = controller.farm
            stall_text.configure(state=tk.NORMAL)
            stall_text.delete("1.0", tk.END)
            if farm is None:
                status_var.set("账号未在线")
                bag_tv.delete(*bag_tv.get_children())
                cash_tv.delete(*cash_tv.get_children())
                bank_tv.delete(*bank_tv.get_children())
                stall_text.insert(tk.END, "账号未在线或尚未进入游戏")
            else:
                fill_src(bag_tv, dict(farm.bag))
                fill_src(cash_tv, dict(farm.cash_bag))
                fill_bank(bank_tv, dict(farm.bank))
                phase = getattr(farm, "stall_phase", "idle")
                status_var.set("摆摊中" if phase == "open" else "未摆摊")
                stall_text.insert(tk.END, "\n".join(stall_status_lines(farm)))
            stall_text.configure(state=tk.DISABLED)

        timer = {"id": None}

        def cancel_timer() -> None:
            if timer["id"] is not None:
                try:
                    win.after_cancel(timer["id"])
                except Exception:
                    pass
                timer["id"] = None

        def tick() -> None:
            timer["id"] = None
            if not win.winfo_exists():
                return
            refresh()
            if auto_var.get():
                timer["id"] = win.after(1000, tick)

        def on_auto_toggle(*_args) -> None:
            cancel_timer()
            if auto_var.get():
                tick()

        auto_var.trace_add("write", on_auto_toggle)
        win.protocol("WM_DELETE_WINDOW", lambda: (cancel_timer(), win.destroy()))

        def do_start() -> None:
            farm = controller.farm
            if farm is None:
                main_log("[界面] 账号未在线,无法摆摊")
                return
            if not pending:
                messagebox.showinfo("摆摊", "待上架列表为空,先从背包/cash 右键添加物品")
                return
            name = name_var.get().strip() or default_name
            preview = "\n".join(
                f"  {src_label(p['bag_type'])}格{p['src_slot']} {item_label(p['item_id'])} "
                f"x{p['qty']} 单价 {p['price']}" for p in pending)
            if not messagebox.askyesno(
                    "开始摆摊 - 确认",
                    f"摊位名「{name}」,将真实上架以下 {len(pending)} 件:\n{preview}\n\n确定开摊?"):
                return
            listings = [(p["bag_type"], p["src_slot"], p["item_id"], p["qty"], p["price"])
                        for p in pending]
            controller.action_queue.put(("stall_open", name, listings))
            main_log(f"[界面] 已请求摆摊「{name}」{len(pending)} 件,剩余量在面板实时刷新")

        def do_stop() -> None:
            if controller.farm is None:
                main_log("[界面] 账号未在线,无法停止摆摊")
                return
            controller.action_queue.put("stall_close")
            main_log(f"[界面] 已请求停止摆摊({controller.account})")

        def do_bank_refresh() -> None:
            # 发开银行包(411154/411644),内容随后由 511320 回流刷 farm.bank,面板自动刷新。
            if controller.farm is None:
                main_log("[界面] 账号未在线,无法刷新银行")
                return
            controller.action_queue.put(("bank_open",))
            main_log("[界面] 已请求开银行(411154),内容稍后回流,面板自动刷新")

        def do_provision() -> None:
            # 一键「补货并上架」:填若干(网店物品, 买几个, 单价),确认后投递
            # ("stall_provision", name, rows) 到 worker 线程,跑 泡点购买->开bank->搬到cash->开摊上架。
            # 售罄会被 stall_check_sold_out 自动收摊。会真的花泡点!
            if controller.farm is None:
                main_log("[界面] 账号未在线,无法补货上架")
                return
            dlg = tk.Toplevel(win)
            dlg.title("补货并上架(泡点购买)")
            dlg.transient(win)
            dlg.grab_set()
            frm = ttk.Frame(dlg, padding=12)
            frm.pack(fill=tk.BOTH, expand=True)

            rows: list[tuple[str, int, int]] = []  # (shop_text, buy_count, price)
            # 缓存:恢复上次提交的清单,do_go 时写回 self.defaults["stall_provision_last"] 并落盘。
            cached = self.defaults.get("stall_provision_last")
            if isinstance(cached, list):
                for entry in cached:
                    try:
                        t, c, pr = str(entry[0]).strip(), int(entry[1]), int(entry[2])
                    except (TypeError, ValueError, IndexError):
                        continue
                    if t:
                        rows.append((t, c, pr))
            last_t, last_c, last_pr = rows[-1] if rows else ("", 1, 0)

            ttk.Label(frm, text="网店物品名(或ID)").grid(row=0, column=0, sticky=tk.W, pady=3)
            iv = tk.StringVar(value=last_t)
            ttk.Entry(frm, textvariable=iv, width=22).grid(row=0, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="购买数量").grid(row=1, column=0, sticky=tk.W, pady=3)
            cv = tk.StringVar(value=str(last_c))
            ttk.Entry(frm, textvariable=cv, width=22).grid(row=1, column=1, sticky="ew", padx=(8, 0))
            ttk.Label(frm, text="上架单价").grid(row=2, column=0, sticky=tk.W, pady=3)
            pv = tk.StringVar(value=str(last_pr))
            ttk.Entry(frm, textvariable=pv, width=22).grid(row=2, column=1, sticky="ew", padx=(8, 0))

            rows_tv = ttk.Treeview(frm, columns=("item", "count", "price"),
                                   show="headings", height=5)
            rows_tv.heading("item", text="物品")
            rows_tv.heading("count", text="购买数量")
            rows_tv.heading("price", text="上架单价")
            rows_tv.column("item", width=200, anchor=tk.W)
            rows_tv.column("count", width=90, anchor=tk.CENTER)
            rows_tv.column("price", width=90, anchor=tk.CENTER)
            rows_tv.grid(row=3, column=0, columnspan=2, sticky="nsew", pady=(8, 0))
            frm.columnconfigure(1, weight=1)
            frm.rowconfigure(3, weight=1)

            def refill_rows() -> None:
                rows_tv.delete(*rows_tv.get_children())
                for text, count, price in rows:
                    iid = resolve_item_id(text, 0, warn=False) or resolve_shop_id(text, 0)
                    label = item_label(iid) if iid else text
                    rows_tv.insert("", tk.END, values=(label, count, price))

            refill_rows()  # 渲染从缓存恢复的上次清单

            def add_row() -> None:
                text = str(iv.get()).strip()
                try:
                    count = int(str(cv.get()).strip())
                    price = int(str(pv.get()).strip())
                except ValueError:
                    messagebox.showwarning("补货并上架", "数量和单价必须是整数")
                    return
                if not text:
                    messagebox.showwarning("补货并上架", "请填网店物品名或ID")
                    return
                if not (resolve_item_id(text, 0, warn=False) or resolve_shop_id(text, 0)):
                    messagebox.showwarning("补货并上架", "物品名/ID 解析不到(游戏表和网店目录都没有)")
                    return
                if count <= 0:
                    messagebox.showwarning("补货并上架", "数量必须 > 0")
                    return
                if price < 0:
                    messagebox.showwarning("补货并上架", "单价不能为负")
                    return
                rows.append((text, count, price))
                refill_rows()

            row_menu = tk.Menu(dlg, tearoff=0)

            def row_rclick(event) -> None:
                r = rows_tv.identify_row(event.y)
                if not r:
                    return
                idx = rows_tv.index(r)
                rows_tv.selection_set(r)
                row_menu.delete(0, tk.END)
                row_menu.add_command(label="从清单移除",
                                     command=lambda: (rows.pop(idx), refill_rows())
                                     if 0 <= idx < len(rows) else None)
                row_menu.tk_popup(event.x_root, event.y_root)

            rows_tv.bind("<Button-3>", row_rclick)

            ttk.Label(frm, text="会真的花泡点!买完自动开bank->搬cash->开摊上架,售罄自动收摊。",
                      foreground="#b00020").grid(row=4, column=0, columnspan=2, sticky=tk.W, pady=(6, 0))

            def do_go() -> None:
                if not rows:
                    messagebox.showinfo("补货并上架", "清单为空,先「添加到清单」")
                    return
                name = name_var.get().strip() or default_name
                preview = "\n".join(
                    f"  {item_label(resolve_item_id(t, 0, warn=False) or resolve_shop_id(t, 0))} "
                    f"x{c} 单价 {pr}" for t, c, pr in rows)
                if not messagebox.askyesno(
                        "补货并上架 - 确认",
                        f"摊位名「{name}」,将用泡点购买并自动上架:\n{preview}\n\n会真的花泡点,确定?"):
                    return
                controller.action_queue.put(("stall_provision", name, list(rows)))
                self.defaults["stall_provision_last"] = [[t, c, pr] for t, c, pr in rows]
                save_defaults_document(self.defaults_file, self.defaults)
                main_log(f"[界面] 已请求补货并上架「{name}」{len(rows)} 种,到货后自动开摊,看日志/面板")
                dlg.destroy()

            bb = ttk.Frame(frm)
            bb.grid(row=5, column=0, columnspan=2, sticky="ew", pady=(10, 0))
            ttk.Button(bb, text="开始(花泡点)", command=do_go).pack(side=tk.RIGHT)
            ttk.Button(bb, text="取消", command=dlg.destroy).pack(side=tk.RIGHT, padx=(0, 6))
            ttk.Button(bb, text="添加到清单", command=add_row).pack(side=tk.LEFT)

        ttk.Button(btns, text="开始摆摊", command=do_start).pack(side=tk.LEFT)
        ttk.Button(btns, text="停止摆摊", command=do_stop).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="补货并上架", command=do_provision).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="刷新银行", command=do_bank_refresh).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Button(btns, text="清空待上架",
                   command=lambda: (pending.clear(), refill_sel())).pack(side=tk.LEFT, padx=(6, 0))
        ttk.Label(btns, text="提示:背包/cash 右键上架;银行右键拖到cash").pack(side=tk.LEFT, padx=(12, 0))
        ttk.Button(btns, text="手动刷新", command=refresh).pack(side=tk.RIGHT)
        ttk.Button(btns, text="关闭",
                   command=lambda: (cancel_timer(), win.destroy())).pack(side=tk.RIGHT, padx=(0, 6))

        tick()

    def on_close(self) -> None:
        for controller in self.controllers.values():
            controller.stop_event.set()
            for sock in controller.take_sockets():
                try:
                    sock.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
                try:
                    sock.close()
                except OSError:
                    pass
        self.controllers.clear()
        set_log_handler(None)
        set_status_handler(None)
        self.root.destroy()


def run_gui(args: argparse.Namespace) -> None:
    import tkinter as tk

    root = tk.Tk()
    OnlineLoginGui(root, args)
    root.mainloop()
