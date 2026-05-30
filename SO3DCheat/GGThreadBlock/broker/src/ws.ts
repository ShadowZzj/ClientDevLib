import { Server as HttpServer } from "http";
import { WebSocketServer, WebSocket } from "ws";
import { InstanceRegistry } from "./instances";
import { AutoReviver } from "./autoReviver";
import { GmReplier } from "./gmReplier";
import { MoneyStats } from "./moneyStats";
import { AutoReviveStats } from "./autoReviveStats";

export function attachWs(
    httpServer: HttpServer,
    registry: InstanceRegistry,
    autoReviver: AutoReviver,
    gmReplier: GmReplier,
    moneyStats: MoneyStats,
    autoReviveStats: AutoReviveStats
) {
    const wss = new WebSocketServer({ server: httpServer, path: "/ws" });

    function snapshot() {
        return {
            type: "snapshot",
            instances: registry.list().map((i) => ({
                pid: i.pid,
                characterName: i.characterName,
                accountName: i.accountName,
                windowTitle: i.windowTitle,
                hostExe: i.hostExe,
                dllVersion: i.dllVersion,
                money: i.status.money,
                lastSeen: i.lastSeen,
            })),
            moneyCharacters: moneyStats.listCharacters(),
            autoReviveCharacters: autoReviveStats.listCharacters(),
        };
    }

    function broadcast(payload: any) {
        const msg = JSON.stringify(payload);
        for (const ws of wss.clients) {
            if (ws.readyState === ws.OPEN) ws.send(msg);
        }
    }

    wss.on("connection", (ws: WebSocket) => {
        // 首连同时发 instances 快照 + autoRevive 状态快照,前端不用再额外 GET。
        ws.send(JSON.stringify(snapshot()));
        ws.send(JSON.stringify({
            type: "autoRevive.states",
            states: autoReviver.listStates(),
        }));
    });

    let pending = false;
    registry.on("change", () => {
        // 合并瞬间 burst 的多次 change（status 1.5s 一次,但首连时 hello+identity+status
        // 可能在同一 tick 进来）。
        if (pending) return;
        pending = true;
        setImmediate(() => {
            pending = false;
            broadcast(snapshot());
        });
    });

    // AutoReviver 状态机增量推送:state 边沿 / 每一行日志 → 单独 frame。
    // 前端只关心当前 characterKey 对应的角色,自己 filter 即可。
    autoReviver.on("state", (name: string, state: any) => {
        broadcast({ type: "autoRevive.state", characterName: name, state });
    });
    autoReviver.on("log", (name: string, line: string) => {
        broadcast({ type: "autoRevive.log", characterName: name, line });
    });

    registry.on("chat", (msg: any) => {
        broadcast({ type: "chat", ...msg });
    });

    gmReplier.on("log", (entry: any) => {
        broadcast({ type: "gmReplier.log", entry });
    });

    return wss;
}
