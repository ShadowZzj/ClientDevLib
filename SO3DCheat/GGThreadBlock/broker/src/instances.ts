import { Socket } from "net";
import { v4 as uuid } from "uuid";
import { EventEmitter } from "events";
import { execFile } from "child_process";

export interface InstanceStatus {
    money?: number;
    hp?: number;
    hpKnown?: boolean;
    clientClosing?: boolean;
    worldRecvIdleMs?: number;
    posX?: number;
    posY?: number;
    posZ?: number;
    mapId?: number;
}

export interface Instance {
    connId: string;
    pid: number;
    characterName?: string;
    accountName?: string;
    windowTitle?: string;
    hostExe?: string;
    dllVersion?: string;
    nativeNpcUiBridgeReady?: boolean;
    nativeDropPickupReady?: boolean;
    status: InstanceStatus;
    lastSeen: number;
}

export interface MoneyUpdateEvent {
    pid: number;
    characterName?: string;
    money: number;
    proto?: number;
    itemSlot?: number;
    source?: string;
    timestamp: number;
}

export interface DialogSelectEvent {
    pid: number;
    characterName?: string;
    npc: number;
    opt: number;
    sub: number;
    timestamp: number;
}

export interface TeleportEvent {
    pid: number;
    characterName?: string;
    destId: number;
    timestamp: number;
}

interface PendingAck {
    resolve: (v: { ok: boolean; detail?: string }) => void;
    reject: (e: Error) => void;
    timer: NodeJS.Timeout;
}

interface ConnState {
    connId: string;
    socket: Socket;
    buffer: string;
    pid?: number;
    pending: Map<string, PendingAck>;
}

export class InstanceRegistry extends EventEmitter {
    private byConnId = new Map<string, ConnState>();
    private byPid = new Map<number, string>(); // pid -> connId
    private instances = new Map<string, Instance>();
    private windowRefreshTimers = new Set<NodeJS.Timeout>();
    // 工作流等长事务可独占某个 PID 的 command 通道。DLL 的 ReadLoop 本身是串行
    // 分发，broker 若让多个 manager 同时做“查询 -> 决策 -> 动作”，中间就可能被
    // 别的动作插入。lease 存 owner token；无 token 的旧 manager/API 在占用期间拒绝。
    private commandLeases = new Map<number, string>();

    handleConnection(socket: Socket): void {
        const connId = uuid();
        const state: ConnState = {
            connId,
            socket,
            buffer: "",
            pending: new Map(),
        };
        this.byConnId.set(connId, state);

        socket.setEncoding("utf8");
        socket.on("data", (chunk: string) => this.onData(state, chunk));
        socket.on("error", (e) => {
            console.warn(`[broker] socket error connId=${connId}: ${e.message}`);
        });
        socket.on("close", () => this.onClose(state));

        console.log(`[broker] connection opened connId=${connId}`);
    }

    private onData(state: ConnState, chunk: string): void {
        state.buffer += chunk;
        for (;;) {
            const i = state.buffer.indexOf("\n");
            if (i < 0) break;
            const line = state.buffer.slice(0, i).trim();
            state.buffer = state.buffer.slice(i + 1);
            if (!line) continue;
            this.handleFrame(state, line);
        }
    }

    private handleFrame(state: ConnState, line: string): void {
        let frame: any;
        try {
            frame = JSON.parse(line);
        } catch (e) {
            console.warn(`[broker] bad json from connId=${state.connId}: ${line}`);
            return;
        }
        const type = frame.type as string;
        if (type !== "status" && type !== "ack" && type !== "pong" && type !== "moneyUpdate") {
            console.log(`[broker] frame type=${type} pid=${frame.pid ?? state.pid}`);
        }
        switch (type) {
            case "hello":
                this.onHello(state, frame);
                break;
            case "identity":
                this.onIdentity(state, frame);
                break;
            case "status":
                this.onStatus(state, frame);
                break;
            case "ack":
                this.onAck(state, frame);
                break;
            case "pong":
                // 心跳回复 — 当前 broker 没主动发 ping,先留 case 防 unknown frame 警告
                break;
            case "chat":
                this.onChat(state, frame);
                break;
            case "dialogSelect":
                this.onDialogSelect(state, frame);
                break;
            case "teleport":
                this.onTeleport(state, frame);
                break;
            case "moneyUpdate":
                this.onMoneyUpdate(state, frame);
                break;
            case "bye":
                console.log(`[broker] bye pid=${frame.pid}`);
                break;
            default:
                console.warn(`[broker] unknown frame type=${type}`);
        }
    }

    private onHello(state: ConnState, f: any): void {
        const pid = Number(f.pid);
        if (!pid) {
            console.warn(`[broker] hello without pid, dropping`);
            state.socket.destroy();
            return;
        }
        // 同 pid 复连:旧实例先下线。常见于游戏没正常退就被强杀,然后 broker 端
        // 还没收到 close,这时新连过来就把残留挤掉。
        const existing = this.byPid.get(pid);
        if (existing && existing !== state.connId) {
            console.log(`[broker] superseding stale conn for pid=${pid}`);
            this.byConnId.get(existing)?.socket.destroy();
        }

        state.pid = pid;
        this.byPid.set(pid, state.connId);
        const inst: Instance = {
            connId: state.connId,
            pid,
            hostExe: f.hostExe,
            dllVersion: f.dllVersion,
            nativeNpcUiBridgeReady: f.nativeNpcUiBridgeReady === true,
            nativeDropPickupReady: f.nativeDropPickupReady === true,
            status: {},
            lastSeen: Date.now(),
        };
        this.instances.set(state.connId, inst);
        console.log(`[broker] hello pid=${pid} dll=${f.dllVersion}`);
        this.emit("change");
        this.refreshWindowIdentity(inst, 500);
        this.refreshWindowIdentity(inst, 3000);
    }

    private onIdentity(state: ConnState, f: any): void {
        const inst = this.instances.get(state.connId);
        if (!inst) return;
        inst.characterName = f.characterName;
        inst.lastSeen = Date.now();
        console.log(`[broker] identity pid=${inst.pid} name=${inst.characterName}`);
        this.emit("change");
    }

    private onStatus(state: ConnState, f: any): void {
        const inst = this.instances.get(state.connId);
        if (!inst) return;
        if (inst.status.money === undefined) {
            console.log(`[broker] first status pid=${inst.pid} money=${f.money} name=${f.characterName ?? "(none)"}`);
        }
        inst.status = {
            money: typeof f.money === "number" ? f.money : Number(f.money) || 0,
            hp: typeof f.hp === "number" ? f.hp : undefined,
            hpKnown: typeof f.hpKnown === "boolean" ? f.hpKnown : undefined,
            clientClosing: typeof f.clientClosing === "boolean" ? f.clientClosing : undefined,
            worldRecvIdleMs: typeof f.worldRecvIdleMs === "number" ? f.worldRecvIdleMs : undefined,
            posX: f.posX,
            posY: f.posY,
            posZ: f.posZ,
            mapId: typeof f.mapId === "number" ? f.mapId : undefined,
        };
        // status 也带 characterName,免得 identity-diff 漏一帧 UI 就一直显示 connecting…
        if (typeof f.characterName === "string" && f.characterName && !inst.characterName) {
            inst.characterName = f.characterName;
            console.log(`[broker] character via status pid=${inst.pid} name=${f.characterName}`);
        }
        inst.lastSeen = Date.now();
        this.emit("change");
        if (inst.characterName && typeof inst.status.money === "number") {
            this.emit("status", inst);
        }
    }

    private onMoneyUpdate(state: ConnState, f: any): void {
        const inst = this.instances.get(state.connId);
        if (!inst) return;

        const money = typeof f.money === "number" ? f.money : Number(f.money);
        if (!Number.isFinite(money)) return;

        if (typeof f.characterName === "string" && f.characterName) {
            inst.characterName = f.characterName;
        }
        inst.status.money = money;
        inst.lastSeen = Date.now();

        this.emit("moneyUpdate", {
            pid: inst.pid,
            characterName: inst.characterName,
            money,
            proto: finiteNumberOrUndefined(f.proto),
            itemSlot: finiteNumberOrUndefined(f.itemSlot),
            source: typeof f.source === "string" ? f.source : undefined,
            timestamp: inst.lastSeen,
        } satisfies MoneyUpdateEvent);
        this.emit("change");
    }

    private onAck(state: ConnState, f: any): void {
        const id = String(f.id || "");
        const p = state.pending.get(id);
        if (!p) {
            console.warn(`[broker] orphan ack id=${id}`);
            return;
        }
        clearTimeout(p.timer);
        state.pending.delete(id);
        p.resolve({ ok: !!f.ok, detail: f.detail });
    }

    updateStatusFromCommand(pid: number, status: any): void {
        const connId = this.byPid.get(pid);
        if (!connId) return;
        const inst = this.instances.get(connId);
        if (!inst || !status || typeof status !== "object") return;
        if (typeof status.hp === "number") inst.status.hp = status.hp;
        if (typeof status.hpKnown === "boolean") inst.status.hpKnown = status.hpKnown;
        if (typeof status.clientClosing === "boolean") inst.status.clientClosing = status.clientClosing;
        if (typeof status.worldRecvIdleMs === "number") {
            inst.status.worldRecvIdleMs = status.worldRecvIdleMs;
        }
        if (typeof status.money === "number") inst.status.money = status.money;
        if (typeof status.posX === "number") inst.status.posX = status.posX;
        if (typeof status.posY === "number") inst.status.posY = status.posY;
        if (typeof status.posZ === "number") inst.status.posZ = status.posZ;
        if (typeof status.mapId === "number") inst.status.mapId = status.mapId;
        if (typeof status.characterName === "string" && status.characterName) inst.characterName = status.characterName;
        inst.lastSeen = Date.now();
        this.emit("change");
        if (inst.characterName && typeof inst.status.money === "number") {
            this.emit("status", inst);
        }
    }

    private onDialogSelect(state: ConnState, f: any): void {
        const inst = this.instances.get(state.connId);
        const npc = finiteNumberOrUndefined(f.npc);
        const opt = finiteNumberOrUndefined(f.opt);
        const sub = finiteNumberOrUndefined(f.sub);
        if (npc === undefined || opt === undefined) return;
        this.emit("dialogSelect", {
            pid: f.pid ?? state.pid,
            characterName: inst?.characterName ?? (typeof f.characterName === "string" ? f.characterName : undefined),
            npc,
            opt,
            sub: sub ?? 1,
            timestamp: Date.now(),
        } satisfies DialogSelectEvent);
    }

    private onTeleport(state: ConnState, f: any): void {
        const inst = this.instances.get(state.connId);
        const destId = finiteNumberOrUndefined(f.destId);
        if (destId === undefined || destId <= 0) return;
        this.emit("teleport", {
            pid: f.pid ?? state.pid,
            characterName: inst?.characterName ?? (typeof f.characterName === "string" ? f.characterName : undefined),
            destId,
            timestamp: Date.now(),
        } satisfies TeleportEvent);
    }

    private onChat(state: ConnState, f: any): void {
        const inst = this.instances.get(state.connId);
        this.emit("chat", {
            pid: f.pid ?? state.pid,
            characterName: inst?.characterName,
            sender: f.sender,
            message: f.message,
            senderUserId: finiteNumberOrUndefined(f.senderUserId),
            channelId: finiteNumberOrUndefined(f.channelId),
            senderProfession: finiteNumberOrUndefined(f.senderProfession),
            senderProfessionName: typeof f.senderProfessionName === "string" ? f.senderProfessionName : undefined,
            timestamp: Date.now(),
        });
    }

    private onClose(state: ConnState): void {
        console.log(`[broker] close connId=${state.connId} pid=${state.pid}`);
        for (const p of state.pending.values()) {
            clearTimeout(p.timer);
            p.reject(new Error("connection closed"));
        }
        state.pending.clear();
        this.byConnId.delete(state.connId);
        if (state.pid && this.byPid.get(state.pid) === state.connId)
            this.byPid.delete(state.pid);
        this.instances.delete(state.connId);
        this.emit("change");
    }

    list(): Instance[] {
        return Array.from(this.instances.values());
    }

    acquireCommandLease(pid: number, ownerToken: string): boolean {
        const owner = String(ownerToken || "").trim();
        if (!owner) return false;
        const connId = this.byPid.get(pid);
        if (!connId) return false;
        const state = this.byConnId.get(connId);
        if (!state) return false;

        const current = this.commandLeases.get(pid);
        if (current) return current === owner;
        // 不从一个已在执行旧命令的连接中间抢 lease；调用方稍后重试/重新运行。
        if (state.pending.size > 0) return false;
        this.commandLeases.set(pid, owner);
        return true;
    }

    releaseCommandLease(pid: number, ownerToken: string): boolean {
        const owner = String(ownerToken || "").trim();
        if (!owner || this.commandLeases.get(pid) !== owner) return false;
        this.commandLeases.delete(pid);
        return true;
    }

    getCommandLeaseOwner(pid: number): string | undefined {
        return this.commandLeases.get(pid);
    }

    private refreshWindowIdentity(inst: Instance, delayMs: number): void {
        const timer = setTimeout(async () => {
            this.windowRefreshTimers.delete(timer);
            const current = this.instances.get(inst.connId);
            if (!current) return;
            try {
                const title = await getMainWindowTitle(current.pid);
                if (!title) return;
                const account = parseAccountNameFromTitle(title);
                const changed = current.windowTitle !== title || current.accountName !== account;
                current.windowTitle = title;
                if (account) current.accountName = account;
                if (changed) {
                    console.log(`[broker] window pid=${current.pid} title="${title}" account=${current.accountName ?? "(none)"}`);
                    this.emit("change");
                }
            } catch (e: any) {
                console.warn(`[broker] window title lookup failed pid=${current.pid}: ${e.message}`);
            }
        }, delayMs);
        this.windowRefreshTimers.add(timer);
    }

    sendCommand(
        pid: number,
        action: string,
        args: any,
        timeoutMs = 5000,
        ownerToken?: string
    ): Promise<{ ok: boolean; detail?: string }> {
        const leaseOwner = this.commandLeases.get(pid);
        if (leaseOwner && leaseOwner !== ownerToken) {
            return Promise.reject(new Error(`pid ${pid} command lease held by ${leaseOwner}`));
        }
        const connId = this.byPid.get(pid);
        if (!connId) return Promise.reject(new Error(`unknown pid ${pid}`));
        const state = this.byConnId.get(connId);
        if (!state) return Promise.reject(new Error(`stale connId for pid ${pid}`));

        const id = uuid();
        const frame = JSON.stringify({ type: "command", id, action, args }) + "\n";
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                state.pending.delete(id);
                reject(new Error("command timed out"));
            }, timeoutMs);
            state.pending.set(id, { resolve, reject, timer });
            state.socket.write(frame, (err) => {
                if (err) {
                    state.pending.delete(id);
                    clearTimeout(timer);
                    reject(err);
                }
            });
        });
    }
}

function parseAccountNameFromTitle(title: string): string | undefined {
    const idx = title.indexOf("|");
    if (idx < 0) return undefined;
    const account = title.slice(idx + 1).trim();
    return account || undefined;
}

function finiteNumberOrUndefined(value: unknown): number | undefined {
    const n = typeof value === "number" ? value : Number(value);
    return Number.isFinite(n) ? n : undefined;
}

function getMainWindowTitle(pid: number): Promise<string> {
    const script = `[Console]::OutputEncoding=[System.Text.Encoding]::UTF8; $p=Get-Process -Id ${pid} -ErrorAction SilentlyContinue; if ($p) { $p.MainWindowTitle }`;
    return new Promise((resolve, reject) => {
        execFile("powershell.exe", ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script], {
            windowsHide: true,
            timeout: 3000,
            encoding: "utf8",
        }, (error, stdout) => {
            if (error) {
                reject(error);
                return;
            }
            resolve(String(stdout || "").trim());
        });
    });
}
