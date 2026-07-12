import http from "http";
import net from "net";
import path from "path";
import { createHttpApp } from "./http";
import { attachWs } from "./ws";
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
import { loadItemNames } from "./itemNames";
import { DailyTaskManager } from "./dailyTaskManager";
import { WorkflowEngine } from "./workflowEngine";

const PIPE_NAME = process.env.GGTB_BROKER_PIPE || "GGTB_BROKER";
const PIPE_PATH = `\\\\.\\pipe\\${PIPE_NAME}`;
const HTTP_PORT = Number(process.env.GGTB_BROKER_PORT) || 7321;
const HTTP_HOST = process.env.GGTB_BROKER_HOST || "127.0.0.1";

function main() {
    const dataDir = process.env.GGTB_DATA_DIR || path.dirname(process.execPath);
    console.log(`[broker] data dir: ${dataDir}`);
    console.log(`[broker] execPath: ${process.execPath}`);

    // 物品名表给拾取过滤页做名字搜索用,一次性读进内存。
    loadItemNames(dataDir);

    const registry = new InstanceRegistry();
    const cashScheduler = new CashScheduler(registry);
    const deathNotifier = new DeathNotifier(registry);
    const gmReplier = new GmReplier(registry);
    const autoReviver = new AutoReviver(registry, () => gmReplier.getGmNames());
    const paodianMonitor = new PaodianMonitor();
    const moneyStats = new MoneyStats(registry);
    const autoReviveStats = new AutoReviveStats(autoReviver);
    const onlineWhitelistSync = new OnlineWhitelistSync(registry, dataDir);
    const buffKeeper = new BuffKeeper(registry);
    const syncManager = new SyncManager(registry);
    const vendorPurchaser = new VendorPurchaser(registry, dataDir);
    const autoTradeManager = new AutoTradeManager(registry);
    const clockworkWasher = new ClockworkWasher(registry);
    const enchantStoneWasher = new EnchantStoneWasher(registry, dataDir);
    const purchaseMonitor = new PurchaseMonitor(registry, paodianMonitor);
    const rewardClaimer = new RewardClaimer(registry);
    const autoComposeManager = new AutoComposeManager(registry);
    const autoTeamManager = new AutoTeamManager(registry, dataDir);
    const reviveTeleportManager = new ReviveTeleportManager(registry, dataDir);
    const pickupFilterManager = new PickupFilterManager(registry);
    const npgManager = new NpgManager(registry);
    const dailyTaskManager = new DailyTaskManager(registry, dataDir);
    const workflowEngine = new WorkflowEngine(registry, dataDir, pickupFilterManager);

    // 命名管道 server。Node 的 net.createServer 直接接受 \\.\pipe\X 作为 listen
    // path,内部走 ConnectNamedPipe 等价路径。DLL 客户端 CreateFileA 上来就能连。
    const pipeServer = net.createServer((socket) => {
        registry.handleConnection(socket);
    });
    pipeServer.on("error", (e) => {
        console.error(`[broker] pipe server error: ${e.message}`);
    });
    pipeServer.listen(PIPE_PATH, () => {
        console.log(`[broker] pipe listening on ${PIPE_PATH}`);
    });

    // HTTP / WS server
    const app = createHttpApp(registry, cashScheduler, deathNotifier, autoReviver, gmReplier, paodianMonitor, moneyStats, autoReviveStats, onlineWhitelistSync, buffKeeper, syncManager, vendorPurchaser, autoTradeManager, clockworkWasher, enchantStoneWasher, purchaseMonitor, rewardClaimer, autoComposeManager, autoTeamManager, reviveTeleportManager, pickupFilterManager, npgManager, dailyTaskManager, workflowEngine);
    const httpServer = http.createServer(app);
    attachWs(httpServer, registry, autoReviver, gmReplier, moneyStats, autoReviveStats);
    httpServer.listen(HTTP_PORT, HTTP_HOST, () => {
        console.log(`[broker] http listening on http://${HTTP_HOST}:${HTTP_PORT}`);
    });

    let shuttingDown = false;
    const shutdown = async () => {
        if (shuttingDown) return;
        shuttingDown = true;
        console.log("[broker] shutting down");
        // 先停止接收新的 Web 控制请求；命名管道保持到 workflow stopPath 尝试完成。
        httpServer.close();
        autoReviver.destroy();
        deathNotifier.destroy();
        cashScheduler.destroy();
        gmReplier.destroy();
        paodianMonitor.destroy();
        moneyStats.destroy();
        autoReviveStats.destroy();
        onlineWhitelistSync.destroy();
        buffKeeper.destroy();
        syncManager.destroy();
        autoTradeManager.destroy();
        clockworkWasher.destroy();
        enchantStoneWasher.destroy();
        purchaseMonitor.destroy();
        rewardClaimer.destroy();
        autoComposeManager.destroy();
        autoTeamManager.destroy();
        reviveTeleportManager.destroy();
        pickupFilterManager.destroy();
        npgManager.destroy();
        dailyTaskManager.destroy();
        try {
            await workflowEngine.destroy();
        } catch (e: any) {
            console.warn(`[broker] workflow shutdown failed: ${e.message || String(e)}`);
        }
        pipeServer.close();
        process.exit(0);
    };
    process.on("SIGINT", () => void shutdown());
    process.on("SIGTERM", () => void shutdown());
}

main();
