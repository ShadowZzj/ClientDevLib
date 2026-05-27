(() => {
    const dot = document.getElementById("ws-dot");
    const wsText = document.getElementById("ws-text");
    const instSel = document.getElementById("instance");
    const instMeta = document.getElementById("instance-meta");
    const recipientInput = document.getElementById("recipient");
    const amountInput = document.getElementById("amount");
    const bodyInput = document.getElementById("body");
    const recipientHist = document.getElementById("recipient-history");
    const sendBtn = document.getElementById("send-btn");
    const sendStatus = document.getElementById("send-status");
    const keysInput = document.getElementById("keys");
    const keysHist = document.getElementById("keys-history");
    const holdMsInput = document.getElementById("hold-ms");
    const sendKeysBtn = document.getElementById("send-keys-btn");
    const sendKeysStatus = document.getElementById("send-keys-status");
    const toast = document.getElementById("toast");
    const refreshBagBtn = document.getElementById("refresh-bag-btn");
    const bagStatus = document.getElementById("bag-status");
    const bagTbody = document.getElementById("bag-tbody");
    const bagSelectAll = document.getElementById("bag-select-all");
    const bagRecipient = document.getElementById("bag-recipient");
    const bagSendBtn = document.getElementById("bag-send-btn");
    const bagSendStatus = document.getElementById("bag-send-status");

    let instances = [];
    let bagItems = [];
    let selectedBagIds = new Set();

    function setOnline(ok) {
        dot.classList.toggle("ok", ok);
        wsText.textContent = ok ? "live" : "disconnected";
    }

    function fmtMoney(n) {
        if (typeof n !== "number") return "?";
        return n.toLocaleString();
    }

    function renderInstances(list) {
        instances = list;
        const prevPid = Number(instSel.value);
        // 排序：有角色名的在前；同名按 pid 排序，保证选项顺序稳定。
        list.sort((a, b) => {
            const an = a.characterName || "~";
            const bn = b.characterName || "~";
            if (an !== bn) return an.localeCompare(bn);
            return a.pid - b.pid;
        });
        instSel.innerHTML = "";
        for (const i of list) {
            const opt = document.createElement("option");
            const label = i.characterName ? `${i.characterName} (pid ${i.pid})` : `(pid ${i.pid} connecting…)`;
            opt.value = String(i.pid);
            opt.textContent = label;
            instSel.appendChild(opt);
        }
        if (prevPid && list.some((i) => i.pid === prevPid)) instSel.value = String(prevPid);
        updateMeta();
    }

    function currentInstance() {
        const pid = Number(instSel.value);
        return instances.find((i) => i.pid === pid);
    }

    function updateMeta() {
        const i = currentInstance();
        if (!i) { instMeta.textContent = "无在线进程"; return; }
        const ageS = ((Date.now() - i.lastSeen) / 1000).toFixed(1);
        instMeta.textContent = `钱包: ${fmtMoney(i.money)}  ·  hostExe: ${i.hostExe || "?"}  ·  上次心跳 ${ageS}s 前`;
    }

    function renderRecipientHistory() {
        const items = JSON.parse(localStorage.getItem("ggtb.recipients") || "[]");
        recipientHist.innerHTML = "";
        for (const r of items) {
            const o = document.createElement("option");
            o.value = r;
            recipientHist.appendChild(o);
        }
    }

    function pushRecipient(name) {
        if (!name) return;
        const items = JSON.parse(localStorage.getItem("ggtb.recipients") || "[]");
        const next = [name, ...items.filter((x) => x !== name)].slice(0, 10);
        localStorage.setItem("ggtb.recipients", JSON.stringify(next));
        renderRecipientHistory();
    }

    // 按键组合历史 — 跟收件人一样,localStorage 存最近 10 条,datalist 自动补全。
    function renderKeysHistory() {
        const items = JSON.parse(localStorage.getItem("ggtb.keys") || "[]");
        keysHist.innerHTML = "";
        for (const k of items) {
            const o = document.createElement("option");
            o.value = k;
            keysHist.appendChild(o);
        }
    }
    function pushKeys(combo) {
        if (!combo) return;
        const items = JSON.parse(localStorage.getItem("ggtb.keys") || "[]");
        const next = [combo, ...items.filter((x) => x !== combo)].slice(0, 10);
        localStorage.setItem("ggtb.keys", JSON.stringify(next));
        renderKeysHistory();
    }

    // "alt, w" / "ctrl+shift+f5" / "18, 87" 都接受。分隔符 , + 空格 都行,
    // 解析后当字符串发给 broker — DLL 端的 nameToVk 会做最终翻译。
    function parseKeyCombo(text) {
        const parts = text.split(/[,+\s]+/).map((s) => s.trim()).filter((s) => s.length > 0);
        return parts.map((p) => /^\d+$/.test(p) ? Number(p) : p);
    }

    function showToast(text, ok) {
        toast.textContent = text;
        toast.className = "toast show " + (ok ? "ok" : "fail");
        setTimeout(() => { toast.className = "toast"; }, 3000);
    }

    instSel.addEventListener("change", updateMeta);

    sendBtn.addEventListener("click", async () => {
        const inst = currentInstance();
        if (!inst) { showToast("未选择实例", false); return; }
        const recipient = recipientInput.value.trim();
        const amount = Number(amountInput.value);
        const body = bodyInput.value;
        if (!recipient || recipient.length > 15 || /[^\x20-\x7e]/.test(recipient)) {
            showToast("收件人无效（1..15 ASCII）", false); return;
        }
        if (!Number.isFinite(amount) || amount <= 0) {
            showToast("金额必须 > 0", false); return;
        }
        if (typeof inst.money === "number" && amount > inst.money) {
            showToast("金额超过钱包余额", false); return;
        }

        sendBtn.disabled = true;
        sendStatus.textContent = "发送中…";
        try {
            const r = await fetch(`/api/command/${inst.pid}`, {
                method: "POST",
                headers: { "content-type": "application/json" },
                body: JSON.stringify({ action: "sendMoneyMail", args: { recipient, amount, body } }),
            }).then((x) => x.json());
            if (r.ok) {
                showToast(`已发送 ${amount} → ${recipient}`, true);
                pushRecipient(recipient);
                sendStatus.textContent = "成功";
            } else {
                showToast(`失败: ${r.detail || r.error || "unknown"}`, false);
                sendStatus.textContent = `失败: ${r.detail || r.error || ""}`;
            }
        } catch (e) {
            showToast(`失败: ${e.message}`, false);
            sendStatus.textContent = `失败: ${e.message}`;
        } finally {
            sendBtn.disabled = false;
        }
    });

    sendKeysBtn.addEventListener("click", async () => {
        const inst = currentInstance();
        if (!inst) { showToast("未选择实例", false); return; }
        const text = keysInput.value.trim();
        if (!text) { showToast("请输入按键组合", false); return; }
        const vks = parseKeyCombo(text);
        if (vks.length === 0) { showToast("解析不到任何按键", false); return; }
        const holdMs = Number(holdMsInput.value) || 80;

        sendKeysBtn.disabled = true;
        sendKeysStatus.textContent = "发送中…";
        try {
            const r = await fetch(`/api/command/${inst.pid}`, {
                method: "POST",
                headers: { "content-type": "application/json" },
                body: JSON.stringify({ action: "sendInput", args: { vks, holdMs } }),
            }).then((x) => x.json());
            if (r.ok) {
                showToast(`已发送: ${text}`, true);
                pushKeys(text);
                sendKeysStatus.textContent = `成功 ${r.detail || ""}`;
            } else {
                showToast(`失败: ${r.detail || r.error || "unknown"}`, false);
                sendKeysStatus.textContent = `失败: ${r.detail || r.error || ""}`;
            }
        } catch (e) {
            showToast(`失败: ${e.message}`, false);
            sendKeysStatus.textContent = `失败: ${e.message}`;
        } finally {
            sendKeysBtn.disabled = false;
        }
    });

    async function pollOnce() {
        try {
            const r = await fetch("/api/instances", { cache: "no-store" });
            if (!r.ok) throw new Error(`HTTP ${r.status}`);
            const list = await r.json();
            renderInstances(list);
            setOnline(true);
        } catch (e) {
            setOnline(false);
        }
    }

    function connectWs() {
        try {
            const proto = location.protocol === "https:" ? "wss" : "ws";
            const url = `${proto}://${location.host}/ws`;
            console.log("[ggtb] connecting WS:", url);
            const ws = new WebSocket(url);
            ws.onopen = () => {
                console.log("[ggtb] ws onopen");
                setOnline(true);
            };
            ws.onclose = (ev) => {
                console.log("[ggtb] ws onclose code=", ev.code, "reason=", ev.reason);
                setOnline(false);
                setTimeout(connectWs, 1500);
            };
            ws.onerror = (ev) => {
                console.log("[ggtb] ws onerror", ev);
            };
            ws.onmessage = (ev) => {
                try {
                    const m = JSON.parse(ev.data);
                    if (m.type === "snapshot") {
                        renderInstances(m.instances);
                        setOnline(true);
                    }
                } catch (e) {
                    console.warn("[ggtb] bad ws msg", e);
                }
            };
        } catch (e) {
            console.warn("[ggtb] connectWs threw", e);
            setTimeout(connectWs, 1500);
        }
    }

    setInterval(updateMeta, 1000);
    // HTTP 兜底:即使 WS 出问题也能看到实例列表 + 发邮件。每 2s 刷一次。
    pollOnce();
    setInterval(pollOnce, 2000);
    renderRecipientHistory();
    renderKeysHistory();
    connectWs();

    // ========== 背包物品 ==========

    function renderBag() {
        bagTbody.innerHTML = "";
        for (const item of bagItems) {
            const tr = document.createElement("tr");
            const checked = selectedBagIds.has(item.bagId);
            if (checked) tr.classList.add("selected");
            tr.innerHTML = `<td><input type="checkbox" data-bag-id="${item.bagId}" ${checked ? "checked" : ""} /></td>` +
                `<td>${item.slotIndex}</td><td>${item.bagId}</td><td>${item.name || ""}</td><td>${item.itemId}</td><td>${item.count}</td>`;
            tr.addEventListener("contextmenu", (e) => { e.preventDefault(); showCtxMenu(e, item); });
            const cb = tr.querySelector("input[type=checkbox]");
            cb.addEventListener("change", () => {
                if (cb.checked) selectedBagIds.add(item.bagId);
                else selectedBagIds.delete(item.bagId);
                tr.classList.toggle("selected", cb.checked);
                updateBagSendBtn();
            });
            bagTbody.appendChild(tr);
        }
        updateBagSendBtn();
    }

    function updateBagSendBtn() {
        bagSendBtn.disabled = selectedBagIds.size === 0 || !bagRecipient.value.trim();
    }

    bagRecipient.addEventListener("input", updateBagSendBtn);

    bagSelectAll.addEventListener("change", () => {
        const checked = bagSelectAll.checked;
        selectedBagIds = checked ? new Set(bagItems.map((i) => i.bagId)) : new Set();
        renderBag();
    });

    refreshBagBtn.addEventListener("click", async () => {
        const inst = currentInstance();
        if (!inst) { showToast("未选择实例", false); return; }
        refreshBagBtn.disabled = true;
        bagStatus.textContent = "加载中…";
        try {
            const r = await fetch(`/api/command/${inst.pid}`, {
                method: "POST",
                headers: { "content-type": "application/json" },
                body: JSON.stringify({ action: "getBagItems", args: {} }),
            }).then((x) => x.json());
            if (r.ok) {
                bagItems = JSON.parse(r.detail || "[]");
                selectedBagIds = new Set();
                bagSelectAll.checked = false;
                renderBag();
                bagStatus.textContent = `${bagItems.length} 件物品`;
            } else {
                bagStatus.textContent = `失败: ${r.detail || r.error || ""}`;
            }
        } catch (e) {
            bagStatus.textContent = `失败: ${e.message}`;
        } finally {
            refreshBagBtn.disabled = false;
        }
    });

    bagSendBtn.addEventListener("click", () => sendSelectedItems());

    async function sendSelectedItems() {
        const inst = currentInstance();
        if (!inst) { showToast("未选择实例", false); return; }
        const recipient = bagRecipient.value.trim();
        if (!recipient || recipient.length > 15) { showToast("收件人无效", false); return; }
        const toSend = bagItems.filter((i) => selectedBagIds.has(i.bagId));
        if (toSend.length === 0) return;

        bagSendBtn.disabled = true;
        bagSendStatus.textContent = `发送中 (0/${toSend.length})…`;
        let ok = 0, fail = 0;
        for (let idx = 0; idx < toSend.length; idx++) {
            const item = toSend[idx];
            bagSendStatus.textContent = `发送中 (${idx + 1}/${toSend.length})…`;
            try {
                const r = await fetch(`/api/command/${inst.pid}`, {
                    method: "POST",
                    headers: { "content-type": "application/json" },
                    body: JSON.stringify({ action: "sendItemMail", args: { recipient, bagId: item.bagId, count: item.count } }),
                }).then((x) => x.json());
                if (r.ok) ok++; else fail++;
            } catch { fail++; }
            if (idx < toSend.length - 1) await new Promise((r) => setTimeout(r, 10000));
        }
        bagSendStatus.textContent = `完成: 成功 ${ok}, 失败 ${fail}`;
        showToast(`邮寄完成: ${ok} 成功, ${fail} 失败`, fail === 0);
        bagSendBtn.disabled = false;
        pushRecipient(recipient);
    }

    // 右键菜单
    let ctxMenu = null;
    function removeCtxMenu() { if (ctxMenu) { ctxMenu.remove(); ctxMenu = null; } }
    document.addEventListener("click", removeCtxMenu);

    function showCtxMenu(e, item) {
        removeCtxMenu();
        ctxMenu = document.createElement("div");
        ctxMenu.className = "ctx-menu";
        ctxMenu.style.left = e.clientX + "px";
        ctxMenu.style.top = e.clientY + "px";

        const sendOne = document.createElement("div");
        sendOne.textContent = `邮寄此物品 (bagId=${item.bagId}, x${item.count})`;
        sendOne.addEventListener("click", async () => {
            removeCtxMenu();
            const recipient = bagRecipient.value.trim() || prompt("输入收件人角色名:");
            if (!recipient) return;
            bagRecipient.value = recipient;
            selectedBagIds = new Set([item.bagId]);
            renderBag();
            await sendSelectedItems();
        });
        ctxMenu.appendChild(sendOne);

        if (selectedBagIds.size > 1) {
            const sendAll = document.createElement("div");
            sendAll.textContent = `邮寄全部选中 (${selectedBagIds.size} 件)`;
            sendAll.addEventListener("click", async () => {
                removeCtxMenu();
                const recipient = bagRecipient.value.trim() || prompt("输入收件人角色名:");
                if (!recipient) return;
                bagRecipient.value = recipient;
                await sendSelectedItems();
            });
            ctxMenu.appendChild(sendAll);
        }

        document.body.appendChild(ctxMenu);
    }
})();
