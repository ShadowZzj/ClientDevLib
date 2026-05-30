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

const PIPE_NAME = process.env.GGTB_BROKER_PIPE || "GGTB_BROKER";
const PIPE_PATH = `\\\\.\\pipe\\${PIPE_NAME}`;
const HTTP_PORT = Number(process.env.GGTB_BROKER_PORT) || 7321;
const HTTP_HOST = process.env.GGTB_BROKER_HOST || "127.0.0.1";

function main() {
    const dataDir = process.env.GGTB_DATA_DIR || path.dirname(process.execPath);
    console.log(`[broker] data dir: ${dataDir}`);
    console.log(`[broker] execPath: ${process.execPath}`);

    const registry = new InstanceRegistry();
    const cashScheduler = new CashScheduler(registry);
    const deathNotifier = new DeathNotifier(registry);
    const autoReviver = new AutoReviver(registry);
    const gmReplier = new GmReplier(registry);
    const paodianMonitor = new PaodianMonitor();
    const moneyStats = new MoneyStats(registry);
    const autoReviveStats = new AutoReviveStats(autoReviver);

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
    const app = createHttpApp(registry, cashScheduler, deathNotifier, autoReviver, gmReplier, paodianMonitor, moneyStats, autoReviveStats);
    const httpServer = http.createServer(app);
    attachWs(httpServer, registry, autoReviver, gmReplier, moneyStats, autoReviveStats);
    httpServer.listen(HTTP_PORT, HTTP_HOST, () => {
        console.log(`[broker] http listening on http://${HTTP_HOST}:${HTTP_PORT}`);
    });

    const shutdown = () => {
        console.log("[broker] shutting down");
        autoReviver.destroy();
        deathNotifier.destroy();
        cashScheduler.destroy();
        gmReplier.destroy();
        paodianMonitor.destroy();
        moneyStats.destroy();
        autoReviveStats.destroy();
        pipeServer.close();
        httpServer.close();
        process.exit(0);
    };
    process.on("SIGINT", shutdown);
    process.on("SIGTERM", shutdown);
}

main();
