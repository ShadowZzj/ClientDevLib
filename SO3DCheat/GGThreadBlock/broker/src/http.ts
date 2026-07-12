import express, { Request, Response } from "express";
import path from "path";
import fs from "fs";
import { InstanceRegistry } from "./instances";
import { CashScheduler } from "./cashScheduler";
import { DeathNotifier } from "./deathNotifier";
import { AutoReviver } from "./autoReviver";
import { GmReplier } from "./gmReplier";
import { PaodianMonitor } from "./paodianMonitor";
import { MoneyStats } from "./moneyStats";
import { AutoReviveStats } from "./autoReviveStats";
import { OnlineWhitelistSync } from "./onlineWhitelistSync";
import { BuffKeeper } from "./buffKeeper";
import { ClockworkWasher } from "./clockworkWasher";
import { EnchantStoneWasher } from "./enchantStoneWasher";
import { SyncManager } from "./syncManager";
import { VendorPurchaser } from "./vendorPurchaser";
import { AutoTradeManager } from "./autoTradeManager";
import { PurchaseMonitor } from "./purchaseMonitor";
import { RewardClaimer } from "./rewardClaimer";
import { AutoComposeManager } from "./autoCompose";
import { AutoTeamManager } from "./autoTeam";
import { ReviveTeleportManager } from "./reviveTeleport";
import { PickupFilterManager } from "./pickupFilterManager";
import { NpgManager } from "./npgManager";
import { searchItems, itemNamesStatus } from "./itemNames";
import { DailyTaskManager } from "./dailyTaskManager";
import { WorkflowEngine } from "./workflowEngine";

const INTERNAL_DUNGEON_ENTRY_COMMANDS = new Set([
    "beginDungeonEntry",
    "openNpcDialogFromWheel",
    "advanceDialogTowardOpt",
    "prepareDungeonEntry",
    "commitDungeonEntry",
    "queryDungeonEntry",
    "cancelDungeonEntry",
    "selectDialogOption",
    "confirmDialog",
]);

export function createHttpApp(
    registry: InstanceRegistry,
    cashScheduler: CashScheduler,
    deathNotifier: DeathNotifier,
    autoReviver: AutoReviver,
    gmReplier: GmReplier,
    paodianMonitor: PaodianMonitor,
    moneyStats: MoneyStats,
    autoReviveStats: AutoReviveStats,
    onlineWhitelistSync: OnlineWhitelistSync,
    buffKeeper: BuffKeeper,
    syncManager: SyncManager,
    vendorPurchaser: VendorPurchaser,
    autoTradeManager: AutoTradeManager,
    clockworkWasher: ClockworkWasher,
    enchantStoneWasher: EnchantStoneWasher,
    purchaseMonitor: PurchaseMonitor,
    rewardClaimer: RewardClaimer,
    autoComposeManager: AutoComposeManager,
    autoTeamManager: AutoTeamManager,
    reviveTeleportManager: ReviveTeleportManager,
    pickupFilterManager: PickupFilterManager,
    npgManager: NpgManager,
    dailyTaskManager: DailyTaskManager,
    workflowEngine: WorkflowEngine
) {
    const app = express();
    app.use(express.json());

    app.get("/api/instances", (_req: Request, res: Response) => {
        res.json(
            registry.list().map((i) => ({
                pid: i.pid,
                characterName: i.characterName,
                accountName: i.accountName,
                windowTitle: i.windowTitle,
                hostExe: i.hostExe,
                dllVersion: i.dllVersion,
                nativeNpcUiBridgeReady: i.nativeNpcUiBridgeReady,
                nativeDropPickupReady: i.nativeDropPickupReady,
                money: i.status.money,
                hp: i.status.hp,
                hpKnown: i.status.hpKnown,
                clientClosing: i.status.clientClosing,
                worldRecvIdleMs: i.status.worldRecvIdleMs,
                mapId: i.status.mapId,
                lastSeen: i.lastSeen,
            }))
        );
    });

    app.post("/api/command/:pid", async (req: Request, res: Response) => {
        const pid = Number(req.params.pid);
        if (!pid) return res.status(400).json({ error: "bad pid" });
        const { action, args } = req.body || {};
        if (!action) return res.status(400).json({ error: "missing action" });
        if (INTERNAL_DUNGEON_ENTRY_COMMANDS.has(String(action))) {
            return res.status(403).json({
                ok: false,
                error: `${action} is internal to the dungeon workflow state machine`,
            });
        }
        if (action === "sendDialogSelectRaw" && Number(args?.option) === 9575) {
            return res.status(403).json({
                ok: false,
                error: "raw opt=9575 is reserved for the dungeon workflow state machine",
            });
        }
        try {
            const r = await registry.sendCommand(pid, action, args || {}, 5000);
            if (action === "getStatus" && r.ok) {
                const detail = typeof r.detail === "string" ? JSON.parse(r.detail || "{}") : r.detail;
                registry.updateStatusFromCommand(pid, detail);
            }
            return res.json(r);
        } catch (e: any) {
            return res.status(502).json({ ok: false, error: e.message });
        }
    });

    app.post("/api/level-up/:pid", async (req: Request, res: Response) => {
        const pid = Number(req.params.pid);
        if (!pid) return res.status(400).json({ ok: false, error: "bad pid" });

        const rawPayload = req.body?.payload ?? 412016;
        const payload = Number(rawPayload);
        if (!Number.isInteger(payload)) {
            return res.status(400).json({ ok: false, error: "payload must be an integer" });
        }

        try {
            const r = await registry.sendCommand(pid, "requestLevelUp", { payload }, 5000);
            return res.json({ ...r, payload });
        } catch (e: any) {
            return res.status(502).json({ ok: false, error: e.message });
        }
    });

    // --- Summoned vendor purchase API ---
    app.get("/api/vendor-purchase/config", (_req: Request, res: Response) => {
        res.json(vendorPurchaser.getConfig());
    });

    app.put("/api/vendor-purchase/config", async (req: Request, res: Response) => {
        try {
            res.json(await vendorPurchaser.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ ok: false, error: e.message || String(e) });
        }
    });

    app.post("/api/vendor-purchase/:pid", async (req: Request, res: Response) => {
        const pid = Number(req.params.pid);
        if (!pid) return res.status(400).json({ ok: false, error: "bad pid" });
        try {
            res.json(await vendorPurchaser.purchase(pid, req.body || {}));
        } catch (e: any) {
            res.status(502).json({ ok: false, error: e.message || String(e), steps: e.steps });
        }
    });

    // --- Online whitelist sync API ---
    app.get("/api/online-whitelist", async (_req: Request, res: Response) => {
        res.json({
            ...onlineWhitelistSync.getSnapshot(),
            file: await onlineWhitelistSync.readWhitelistFile(),
        });
    });

    app.put("/api/online-whitelist", async (req: Request, res: Response) => {
        try {
            res.json({ config: await onlineWhitelistSync.setConfig(req.body || {}) });
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });

    app.post("/api/online-whitelist/sync-now", async (_req: Request, res: Response) => {
        try {
            await onlineWhitelistSync.triggerSync();
            res.json({ ok: true, ...onlineWhitelistSync.getSnapshot() });
        } catch (e: any) {
            res.status(502).json({ ok: false, error: e.message || String(e) });
        }
    });

    // --- Money stats API ---
    app.get("/api/money-stats/characters", (_req: Request, res: Response) => {
        res.json(moneyStats.listCharacters());
    });

    app.get("/api/money-stats/daily-gain", (_req: Request, res: Response) => {
        res.json(moneyStats.allDailyGain());
    });

    app.get("/api/money-stats/:name", (req: Request, res: Response) => {
        const unit = req.query.unit === "day" ? "day" : "hour";
        res.json(moneyStats.snapshot(req.params.name, unit));
    });

    // --- Auto revive stats API ---
    app.get("/api/auto-revive-stats/characters", (_req: Request, res: Response) => {
        res.json(autoReviveStats.listCharacters());
    });

    app.get("/api/auto-revive-stats/daily", (_req: Request, res: Response) => {
        res.json(autoReviveStats.allDaily());
    });

    app.get("/api/auto-revive-stats/:name", (req: Request, res: Response) => {
        const unit = req.query.unit === "day" ? "day" : "hour";
        res.json(autoReviveStats.snapshot(req.params.name, unit));
    });

    // --- Cash schedule API ---
    app.get("/api/cash-schedule", (_req: Request, res: Response) => {
        res.json(cashScheduler.getEntries());
    });

    app.post("/api/cash-schedule", (req: Request, res: Response) => {
        const { characterName, itemName, intervalMs } = req.body || {};
        if (!characterName || !itemName || !intervalMs)
            return res.status(400).json({ error: "missing characterName, itemName or intervalMs" });
        const inst = registry.list().find((i) => i.characterName === characterName);
        cashScheduler.addOrUpdate({
            pid: inst?.pid || 0,
            characterName,
            itemName,
            intervalMs: Number(intervalMs),
        });
        res.json({ ok: true });
    });

    app.delete("/api/cash-schedule", (req: Request, res: Response) => {
        const { characterName, itemName } = req.body || {};
        if (!characterName || !itemName)
            return res.status(400).json({ error: "missing characterName or itemName" });
        const removed = cashScheduler.remove(characterName, itemName);
        res.json({ ok: removed });
    });

    // --- Death notify API ---
    app.get("/api/death-notify", (_req: Request, res: Response) => {
        res.json(deathNotifier.getConfig());
    });

    app.post("/api/death-notify", (req: Request, res: Response) => {
        const { phone, enabledCharacters, smsEnabled, discordEnabled, discordWebhook, discordProxy } = req.body || {};
        deathNotifier.setConfig({ phone, enabledCharacters, smsEnabled, discordEnabled, discordWebhook, discordProxy });
        res.json({ ok: true });
    });

    // --- Auto revive API ---
    // configs:每个角色一份脚本 + autoRun + delayMin。前端 GET/PUT/DELETE。
    // state: idle / pending / running 状态机的当前快照(broker 侧权威)。
    // log:   每角色最近 200 行运行日志,WS 也会增量推。
    app.get("/api/auto-revive/configs", (_req: Request, res: Response) => {
        res.json(autoReviver.listConfigs());
    });
    app.get("/api/auto-revive/configs/:name", (req: Request, res: Response) => {
        const c = autoReviver.getConfig(req.params.name);
        if (!c) return res.status(404).json({ error: "not found" });
        res.json(c);
    });
    app.put("/api/auto-revive/configs/:name", (req: Request, res: Response) => {
        const body = req.body || {};
        const cfg = autoReviver.setConfig(req.params.name, { ...body, characterName: req.params.name });
        res.json(cfg);
    });
    app.delete("/api/auto-revive/configs/:name", (req: Request, res: Response) => {
        const ok = autoReviver.deleteConfig(req.params.name);
        res.json({ ok });
    });
    app.get("/api/auto-revive/states", (_req: Request, res: Response) => {
        res.json(autoReviver.listStates());
    });
    app.get("/api/auto-revive/states/:name", (req: Request, res: Response) => {
        res.json(autoReviver.getState(req.params.name));
    });
    app.get("/api/auto-revive/logs/:name", (req: Request, res: Response) => {
        res.json(autoReviver.getLog(req.params.name));
    });
    // 控制端点:取消 pending / 中止 running / 立刻 run。
    app.post("/api/auto-revive/cancel/:name", (req: Request, res: Response) => {
        res.json({ ok: autoReviver.cancelPending(req.params.name) });
    });
    app.post("/api/auto-revive/abort/:name", (req: Request, res: Response) => {
        res.json({ ok: autoReviver.abortRunning(req.params.name) });
    });
    app.post("/api/auto-revive/run-now/:name", async (req: Request, res: Response) => {
        const r = await autoReviver.runNow(req.params.name);
        res.json(r);
    });
    app.post("/api/auto-revive/run-all-dead", async (_req: Request, res: Response) => {
        const r = await autoReviver.runAllDeadNow();
        res.json(r);
    });

    // --- Auto revive 全局模板库 API ---
    // stepTemplates: 命名的脚本步骤集合(不含定点坐标),任意角色可套用。
    // dialogTemplates: 给 sendDialogSelectRaw 的 (npcId, option) 命名。
    app.get("/api/auto-revive/step-templates", (_req: Request, res: Response) => {
        res.json(autoReviver.listStepTemplates());
    });
    app.put("/api/auto-revive/step-templates", (req: Request, res: Response) => {
        try {
            res.json(autoReviver.saveStepTemplate(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.delete("/api/auto-revive/step-templates/:id", (req: Request, res: Response) => {
        res.json({ ok: autoReviver.deleteStepTemplate(req.params.id) });
    });
    app.get("/api/auto-revive/dialog-templates", (_req: Request, res: Response) => {
        res.json(autoReviver.listDialogTemplates());
    });
    app.put("/api/auto-revive/dialog-templates", (req: Request, res: Response) => {
        try {
            res.json(autoReviver.saveDialogTemplate(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.delete("/api/auto-revive/dialog-templates/:id", (req: Request, res: Response) => {
        res.json({ ok: autoReviver.deleteDialogTemplate(req.params.id) });
    });

    // --- GM 回复 API ---
    app.get("/api/gm-replier/config", (_req: Request, res: Response) => {
        res.json(gmReplier.getConfig());
    });
    app.put("/api/gm-replier/config", (req: Request, res: Response) => {
        gmReplier.setConfig(req.body || {});
        res.json(gmReplier.getConfig());
    });
    app.get("/api/gm-replier/logs", (_req: Request, res: Response) => {
        res.json(gmReplier.getLogs());
    });
    app.post("/api/gm-replier/logs/clear", (_req: Request, res: Response) => {
        gmReplier.clearLogs();
        res.json({ ok: true });
    });
    // 测试:模拟 GM 发某句话,只返回模型预测的回复,不发游戏、不写日志。
    app.post("/api/gm-replier/test", async (req: Request, res: Response) => {
        const { message, characterName } = req.body || {};
        if (!message || typeof message !== "string") {
            return res.status(400).json({ error: "missing message" });
        }
        try {
            const result = await gmReplier.previewReply(message, characterName);
            res.json(result);
        } catch (e: any) {
            res.status(500).json({ error: e?.message ?? String(e) });
        }
    });

    // --- Paodian API ---
    app.get("/api/paodian/config", (_req: Request, res: Response) => {
        res.json(paodianMonitor.getConfig());
    });
    app.put("/api/paodian/config", (req: Request, res: Response) => {
        try {
            res.json(paodianMonitor.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.get("/api/paodian/accounts", (_req: Request, res: Response) => {
        res.json(paodianMonitor.list());
    });
    app.post("/api/paodian/accounts", (req: Request, res: Response) => {
        const { username, password } = req.body || {};
        try {
            res.json(paodianMonitor.addOrUpdate(username, password));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.delete("/api/paodian/accounts/:username", (req: Request, res: Response) => {
        res.json({ ok: paodianMonitor.remove(req.params.username) });
    });
    app.get("/api/paodian/shop-items", async (req: Request, res: Response) => {
        try {
            res.json(await paodianMonitor.getShopItems(req.query.force === "1"));
        } catch (e: any) {
            res.status(502).json({ error: e.message || String(e) });
        }
    });
    app.post("/api/paodian/purchase/:username", async (req: Request, res: Response) => {
        const { itemID, itemCount } = req.body || {};
        try {
            res.json(await paodianMonitor.purchase(req.params.username, Number(itemID), Number(itemCount)));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    // 批量购买:对 usernames[] 执行同一商品 + 同一数量,先做余额预检(任一不足整体不执行)。
    app.post("/api/paodian/purchase-batch", async (req: Request, res: Response) => {
        const { usernames, itemID, itemCount } = req.body || {};
        try {
            res.json(await paodianMonitor.purchaseBatch(usernames, Number(itemID), Number(itemCount)));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    // 购买记录(含批量),最新在前。
    app.get("/api/paodian/records", (_req: Request, res: Response) => {
        res.json(paodianMonitor.listRecords());
    });
    app.delete("/api/paodian/records", (_req: Request, res: Response) => {
        res.json({ ok: true, cleared: paodianMonitor.clearRecords() });
    });
    app.post("/api/paodian/refresh", async (_req: Request, res: Response) => {
        res.json(await paodianMonitor.refreshAll());
    });
    app.post("/api/paodian/refresh/:username", async (req: Request, res: Response) => {
        try {
            res.json(await paodianMonitor.refresh(req.params.username));
        } catch (e: any) {
            res.status(404).json({ error: e.message || String(e) });
        }
    });

    // --- Purchase monitor API (监控购买) ---
    // 监控某角色 cash 背包某物品数量,低于阈值就用该角色账号去泡点买并搬进 cash 背包。
    app.get("/api/purchase-monitor/config", (_req: Request, res: Response) => {
        res.json(purchaseMonitor.getConfig());
    });
    app.put("/api/purchase-monitor/config", (req: Request, res: Response) => {
        try {
            res.json(purchaseMonitor.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.get("/api/purchase-monitor/entries", (_req: Request, res: Response) => {
        res.json(purchaseMonitor.getEntries());
    });
    app.post("/api/purchase-monitor/entries", (req: Request, res: Response) => {
        try {
            res.json(purchaseMonitor.addOrUpdateEntry(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.delete("/api/purchase-monitor/entries", (req: Request, res: Response) => {
        const characterName = String(req.query.characterName || "");
        const itemName = String(req.query.itemName || "");
        res.json({ ok: purchaseMonitor.removeEntry(characterName, itemName) });
    });
    // 手动触发(测试用):买 count 个并搬进 cash 背包,忽略最小间隔,不更新 lastTriggered。
    app.post("/api/purchase-monitor/run", async (req: Request, res: Response) => {
        try {
            const { characterName, itemName, itemId, count } = req.body || {};
            res.json(
                await purchaseMonitor.manualRun({
                    characterName,
                    itemName,
                    itemId: itemId !== undefined ? Number(itemId) : undefined,
                    count: Number(count),
                })
            );
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });

    // --- Buff keeper API ---
    // 配置:每个角色一份 { enabled, pollMs, rules[] }。前端 GET/PUT/DELETE。
    // rules 每条 { id, enabled, buffId|buffName, skillId, targetId, minRecastMs }。
    app.get("/api/buff-keeper/configs", (_req: Request, res: Response) => {
        res.json(buffKeeper.listConfigs());
    });
    app.get("/api/buff-keeper/configs/:name", (req: Request, res: Response) => {
        const c = buffKeeper.getConfig(req.params.name);
        if (!c) return res.status(404).json({ error: "not found" });
        res.json(c);
    });
    app.put("/api/buff-keeper/configs/:name", (req: Request, res: Response) => {
        const cfg = buffKeeper.setConfig(req.params.name, req.body || {});
        res.json(cfg);
    });
    app.delete("/api/buff-keeper/configs/:name", (req: Request, res: Response) => {
        res.json({ ok: buffKeeper.deleteConfig(req.params.name) });
    });

    // --- 每日奖励领取 (reward-claim) API ---
    // 每角色一份 { enabled, doneDate, lastAttempt, lastResult }。前端多选角色开关。
    // 开启后角色在线 10s 触发一次领取(在线+签到),当天全领完则当天不再开窗。
    app.get("/api/reward-claim/configs", (_req: Request, res: Response) => {
        res.json(rewardClaimer.listConfigs());
    });
    app.get("/api/reward-claim/configs/:name", (req: Request, res: Response) => {
        const c = rewardClaimer.getConfig(req.params.name);
        if (!c) return res.status(404).json({ error: "not found" });
        res.json(c);
    });
    app.put("/api/reward-claim/configs/:name", (req: Request, res: Response) => {
        res.json(rewardClaimer.setConfig(req.params.name, req.body || {}));
    });
    app.delete("/api/reward-claim/configs/:name", (req: Request, res: Response) => {
        res.json({ ok: rewardClaimer.deleteConfig(req.params.name) });
    });
    // 立即领取(测试 / 手动):无视 10s 与当天已领完标记,角色须在线。
    app.post("/api/reward-claim/run-now/:name", async (req: Request, res: Response) => {
        res.json(await rewardClaimer.runNow(req.params.name));
    });

    // --- 每日任务 (daily-task) API ---
    app.get("/api/daily-task/configs", (_req: Request, res: Response) => {
        res.json(dailyTaskManager.listConfigs());
    });
    app.get("/api/daily-task/configs/:name", (req: Request, res: Response) => {
        const config = dailyTaskManager.getConfig(req.params.name);
        if (!config) return res.status(404).json({ error: "not found" });
        res.json(config);
    });
    app.put("/api/daily-task/configs/:name", (req: Request, res: Response) => {
        res.json(dailyTaskManager.setConfig(req.params.name, req.body || {}));
    });
    app.delete("/api/daily-task/configs/:name", (req: Request, res: Response) => {
        res.json({ ok: dailyTaskManager.deleteConfig(req.params.name) });
    });
    app.post("/api/daily-task/run-now/:name", async (req: Request, res: Response) => {
        res.json(await dailyTaskManager.runNow(req.params.name));
    });

    // --- 自动洗发条 (clockwork-washer) API ---
    // 配置:每个角色一份 { enabled, slotIndex, springType, targetGrade, requiredAttrs[],
    // minMatchCount, pollMs, maxWashes } + 运行时 { washCount, status, lastGrade, lastAttrs }。
    // 实时查物品/属性表走通用 /api/command/:pid (queryClockwork / getSpringAttrTable)。
    app.get("/api/clockwork-washer/configs", (_req: Request, res: Response) => {
        res.json(clockworkWasher.listConfigs());
    });
    app.get("/api/clockwork-washer/configs/:name", (req: Request, res: Response) => {
        const c = clockworkWasher.getConfig(req.params.name);
        if (!c) return res.status(404).json({ error: "not found" });
        res.json(c);
    });
    app.put("/api/clockwork-washer/configs/:name", (req: Request, res: Response) => {
        const cfg = clockworkWasher.setConfig(req.params.name, req.body || {});
        res.json(cfg);
    });
    app.delete("/api/clockwork-washer/configs/:name", (req: Request, res: Response) => {
        res.json({ ok: clockworkWasher.deleteConfig(req.params.name) });
    });

    // --- 石头附魔自动洗 (enchant-stone) API ---
    app.get("/api/enchant-stone/configs", (_req: Request, res: Response) => {
        res.json(enchantStoneWasher.listConfigs());
    });
    app.get("/api/enchant-stone/configs/:name", (req: Request, res: Response) => {
        const cfg = enchantStoneWasher.getConfig(req.params.name);
        if (!cfg) return res.status(404).json({ ok: false, error: "config not found" });
        res.json(cfg);
    });
    app.put("/api/enchant-stone/configs/:name", (req: Request, res: Response) => {
        try {
            res.json(enchantStoneWasher.setConfig(req.params.name, req.body || {}));
        } catch (e: any) {
            res.status(400).json({ ok: false, error: e.message || String(e) });
        }
    });
    app.delete("/api/enchant-stone/configs/:name", (req: Request, res: Response) => {
        res.json({ ok: enchantStoneWasher.deleteConfig(req.params.name) });
    });
    app.get("/api/enchant-stone/state/:name", (req: Request, res: Response) => {
        const state = enchantStoneWasher.getState(req.params.name);
        if (!state) return res.status(404).json({ ok: false, error: "config not found" });
        res.json(state);
    });
    app.get("/api/enchant-stone/logs/:name", (req: Request, res: Response) => {
        res.json(enchantStoneWasher.getLogs(req.params.name));
    });
    app.post("/api/enchant-stone/refresh/:name", async (req: Request, res: Response) => {
        const result = await enchantStoneWasher.refresh(req.params.name);
        if (!result.ok) return res.status(400).json(result);
        res.json(result);
    });
    app.post("/api/enchant-stone/start/:name", (req: Request, res: Response) => {
        const result = enchantStoneWasher.start(req.params.name);
        if (!result.ok) return res.status(400).json(result);
        res.json(result);
    });
    app.post("/api/enchant-stone/stop/:name", (req: Request, res: Response) => {
        const result = enchantStoneWasher.stop(req.params.name);
        if (!result.ok) return res.status(404).json(result);
        res.json(result);
    });

    // --- 自动合成技能宝石 (auto-compose) API ---
    // 配置:每个角色一份 { enabled, targetRank(1=G/2=DG/3=XG/4=SG), intervalMs, maxBatches }
    // + 运行时 { batchCount, status, lastError, lastRunAt, lastGroups[] }。
    // broker 轮询读背包分组(listComposeGems)→ 逐级把低于目标品级的同级宝石三合一(composeGems)。
    app.get("/api/auto-compose/configs", (_req: Request, res: Response) => {
        res.json(autoComposeManager.listConfigs());
    });
    app.get("/api/auto-compose/configs/:name", (req: Request, res: Response) => {
        const c = autoComposeManager.getConfig(req.params.name);
        if (!c) return res.status(404).json({ error: "not found" });
        res.json(c);
    });
    app.put("/api/auto-compose/configs/:name", (req: Request, res: Response) => {
        const cfg = autoComposeManager.setConfig(req.params.name, req.body || {});
        res.json(cfg);
    });
    app.delete("/api/auto-compose/configs/:name", (req: Request, res: Response) => {
        res.json({ ok: autoComposeManager.deleteConfig(req.params.name) });
    });

    // --- 同步 (sync) API ---
    // 单一配置:{ enabled, masterName, slaveNames[], followIntervalMs, mirrorPosition,
    // mirrorDialog, posEpsilon }。前端 GET/PUT。主角色移动 + NPC 对话镜像给副角色。
    app.get("/api/sync/config", (_req: Request, res: Response) => {
        res.json(syncManager.getConfig());
    });
    app.put("/api/sync/config", (req: Request, res: Response) => {
        res.json(syncManager.setConfig(req.body || {}));
    });

    // --- 自动交易 (auto-trade) API ---
    // 全局单一配置:{ enabled, acceptAll, autoLockConfirm, acceptDelayMs,
    // confirmDelayMs, whitelist[] }。所有角色共用,改了即推给全部在线实例。
    app.get("/api/auto-trade/config", (_req: Request, res: Response) => {
        res.json(autoTradeManager.getConfig());
    });
    app.put("/api/auto-trade/config", (req: Request, res: Response) => {
        res.json(autoTradeManager.setConfig(req.body || {}));
    });

    // --- 自动组队 (auto-team) API ---
    // config: 轻量持久化 { leaderName, selectedNames[] },仅为多开页面刷新后回填选择。
    // party-list: 用一个在线角色拉一次全量队伍列表(创建人/分配/人数)。
    // join: 对选中角色(pids 或 characterNames)各下发一次加入,按 leaderName 匹配队长(或直接 partyId)。
    app.get("/api/auto-team/config", (_req: Request, res: Response) => {
        res.json(autoTeamManager.getConfig());
    });
    app.put("/api/auto-team/config", async (req: Request, res: Response) => {
        try {
            res.json(await autoTeamManager.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.get("/api/auto-team/party-list/:pid", async (req: Request, res: Response) => {
        const pid = Number(req.params.pid);
        if (!pid) return res.status(400).json({ ok: false, error: "bad pid" });
        try {
            res.json(await autoTeamManager.fetchPartyList(pid));
        } catch (e: any) {
            res.status(502).json({ ok: false, error: e.message || String(e) });
        }
    });
    app.post("/api/auto-team/join", async (req: Request, res: Response) => {
        try {
            const results = await autoTeamManager.join(req.body || {});
            res.json({ ok: results.every((r) => r.ok), results });
        } catch (e: any) {
            res.status(400).json({ ok: false, error: e.message || String(e) });
        }
    });

    // --- 复活传送 (revive-teleport) API ---
    // config: 废墟传送的可配置定义 { ruins: [{name, viaCityMode, walkX/Y, npcId, dialogOption, ...}] }。
    //         固定城镇(狮子城/乐园镇)写死在 manager 里,不在配置内。
    // dispatch: 对选中账号批量下发。{ kind:"town", townMode } 或 { kind:"ruins", ruinsId }
    //           + characterNames/pids。town 一条 reviveToTown;ruins 走「回城->等->寻路->发对话」序列。
    app.get("/api/revive-teleport/config", (_req: Request, res: Response) => {
        res.json(reviveTeleportManager.getConfig());
    });
    app.put("/api/revive-teleport/config", async (req: Request, res: Response) => {
        try {
            res.json(await reviveTeleportManager.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.post("/api/revive-teleport/dispatch", async (req: Request, res: Response) => {
        try {
            const results = await reviveTeleportManager.dispatch(req.body || {});
            res.json({ ok: results.every((r) => r.ok), results });
        } catch (e: any) {
            res.status(400).json({ ok: false, error: e.message || String(e) });
        }
    });

    // --- 自动拾取过滤 (pickup-filter) API ---
    // 全局单一配置:{ mode, items, fuzzyKeywords }。所有角色共用。模糊关键字由 broker
    // 根据 item_names.json 展开，与精确 items 合并后只把 mode + itemIds 下发给 DLL。item-search
    // 做名字搜索,返回 [{itemId, name}],前端据此挑物品加进白名单。
    app.get("/api/pickup-filter/config", (_req: Request, res: Response) => {
        res.json(pickupFilterManager.getConfig());
    });
    app.put("/api/pickup-filter/config", (req: Request, res: Response) => {
        try {
            res.json(pickupFilterManager.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });
    app.get("/api/pickup-filter/item-search", (req: Request, res: Response) => {
        const q = String(req.query.q || "");
        const limit = Math.min(200, Math.max(1, Number(req.query.limit) || 50));
        res.json({ ...itemNamesStatus(), items: searchItems(q, limit) });
    });

    // --- 附近玩家停手 (NPG) API ---
    // 全局单一配置:{ enabled, holdSeconds }。所有角色共用,改了即推给全部在线实例
    // (setNpgConfig)。holdSeconds = 看到人后保持停手的最短秒数(期间再看到人刷新计时)。
    app.get("/api/npg/config", (_req: Request, res: Response) => {
        res.json(npgManager.getConfig());
    });
    app.put("/api/npg/config", (req: Request, res: Response) => {
        try {
            res.json(npgManager.setConfig(req.body || {}));
        } catch (e: any) {
            res.status(400).json({ error: e.message || String(e) });
        }
    });

    // --- Web 可编排工作流 API ---
    app.get("/api/workflows/catalog", (_req: Request, res: Response) => {
        res.json(workflowEngine.getCatalog());
    });
    app.get("/api/workflows", (_req: Request, res: Response) => {
        res.json(workflowEngine.listWorkflows());
    });
    app.get("/api/workflows/:id", (req: Request, res: Response) => {
        const workflow = workflowEngine.getWorkflow(req.params.id);
        if (!workflow) return res.status(404).json({ ok: false, error: "workflow not found" });
        res.json(workflow);
    });
    app.put("/api/workflows/:id", (req: Request, res: Response) => {
        try {
            res.json(workflowEngine.setWorkflow(req.params.id, req.body || {}));
        } catch (e: any) {
            res.status(400).json({ ok: false, error: e.message || String(e) });
        }
    });
    app.delete("/api/workflows/:id", (req: Request, res: Response) => {
        try {
            res.json({ ok: workflowEngine.deleteWorkflow(req.params.id) });
        } catch (e: any) {
            res.status(500).json({ ok: false, error: e.message || String(e) });
        }
    });
    app.post("/api/workflows/:id/run", (req: Request, res: Response) => {
        try {
            const result = workflowEngine.startWorkflow(req.params.id, req.body?.pids);
            const ok = result.runs.length > 0 && result.errors.length === 0;
            res.status(result.runs.length > 0 ? 200 : 400).json({ ok, ...result });
        } catch (e: any) {
            const notFound = String(e.message || e).startsWith("workflow not found:");
            res.status(notFound ? 404 : 400).json({ ok: false, error: e.message || String(e) });
        }
    });
    app.get("/api/workflow-runs", (_req: Request, res: Response) => {
        res.json(workflowEngine.listRuns());
    });
    app.get("/api/workflow-runs/:id", (req: Request, res: Response) => {
        const run = workflowEngine.getRun(req.params.id);
        if (!run) return res.status(404).json({ ok: false, error: "run not found" });
        res.json(run);
    });
    app.post("/api/workflow-runs/:id/pause", async (req: Request, res: Response) => {
        const run = await workflowEngine.pauseRun(req.params.id);
        if (!run) return res.status(404).json({ ok: false, error: "run not found" });
        res.json({ ok: true, run });
    });
    app.post("/api/workflow-runs/:id/resume", (req: Request, res: Response) => {
        const run = workflowEngine.resumeRun(req.params.id);
        if (!run) return res.status(404).json({ ok: false, error: "run not found" });
        res.json({ ok: true, run });
    });
    app.post("/api/workflow-runs/:id/stop", async (req: Request, res: Response) => {
        const run = await workflowEngine.stopRun(req.params.id);
        if (!run) return res.status(404).json({ ok: false, error: "run not found" });
        res.json({ ok: true, run });
    });

    // Embedded mode: serve from in-memory assets; otherwise use filesystem
    if (process.env.EMBEDDED === "1") {
        let assets: Record<string, string> = {};
        try { assets = require("./_embedded_assets").default; } catch {}
        const mimeTypes: Record<string, string> = {
            ".html": "text/html",
            ".js": "application/javascript",
            ".css": "text/css",
            ".json": "application/json",
            ".svg": "image/svg+xml",
            ".png": "image/png",
            ".ico": "image/x-icon",
            ".woff": "font/woff",
            ".woff2": "font/woff2",
        };
        app.use("/", (req: Request, res: Response, next) => {
            let filePath = req.path === "/" ? "index.html" : req.path.replace(/^\//, "");
            const data = assets[filePath];
            if (!data) return next();
            const ext = path.extname(filePath);
            res.setHeader("Content-Type", mimeTypes[ext] || "application/octet-stream");
            res.send(Buffer.from(data, "base64"));
        });
    } else {
        const webDistDir = path.resolve(__dirname, "..", "web-dist");
        const webDir = fs.existsSync(webDistDir) ? webDistDir : path.resolve(__dirname, "..", "web");
        app.use("/", express.static(webDir));
    }

    return app;
}
