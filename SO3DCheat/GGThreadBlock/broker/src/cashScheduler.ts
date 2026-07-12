import fs from "fs";
import path from "path";
import { InstanceRegistry } from "./instances";
import { classifyLocalPlayerStatus } from "./workflowSignals";

export interface CashScheduleEntry {
    pid: number;
    characterName: string;
    itemName: string;
    intervalMs: number;
    lastUsed: number; // epoch ms, 0 = never
}

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "cash_schedule.json"
);

export class CashScheduler {
    private entries: CashScheduleEntry[] = [];
    private timer: NodeJS.Timeout | null = null;
    private registry: InstanceRegistry;

    constructor(registry: InstanceRegistry) {
        this.registry = registry;
        this.load();
        this.start();
    }

    private load(): void {
        try {
            if (fs.existsSync(PERSIST_FILE)) {
                const raw = fs.readFileSync(PERSIST_FILE, "utf8");
                this.entries = JSON.parse(raw);
                console.log(`[cashScheduler] loaded ${this.entries.length} entries`);
            }
        } catch (e: any) {
            console.warn(`[cashScheduler] load failed: ${e.message}`);
            this.entries = [];
        }
    }

    private save(): void {
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify(this.entries, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[cashScheduler] save failed: ${e.message}`);
        }
    }

    private start(): void {
        this.timer = setInterval(() => this.tick(), 10_000);
    }

    private async tick(): Promise<void> {
        const now = Date.now();
        const online = this.registry.list();
        const nameToLivePid = new Map<string, number>();
        for (const inst of online) {
            if (inst.characterName) nameToLivePid.set(inst.characterName, inst.pid);
        }

        for (const entry of this.entries) {
            const livePid = nameToLivePid.get(entry.characterName);
            if (livePid === undefined) continue;

            if (livePid !== entry.pid) {
                entry.pid = livePid;
            }

            const elapsed = now - entry.lastUsed;
            if (elapsed < entry.intervalMs) continue;

            // 到点了,但角色得活着才用。死着就跳过本轮 — 下个 tick 会再问一次,
            // 一旦 HP > 0 立刻使用。注:不更新 lastUsed,所以"该用的没用上"的
            // 这段时间不会被吃掉,活过来马上补。
            //
            // getStatus 失败(超时 / pipe 异常)按"不确定"处理 → 跳过本轮,
            // 不发 useCashItem,防止死亡时误用。
            try {
                const statusResult = await this.registry.sendCommand(
                    livePid, "getStatus", {}, 3000
                );
                if (!statusResult.ok) {
                    console.warn(`[cashScheduler] getStatus failed for ${entry.characterName}: ${statusResult.detail} — skip this tick`);
                    continue;
                }
                const status = JSON.parse(statusResult.detail || "{}");
                const statusKind = classifyLocalPlayerStatus(status);
                if (statusKind === "dead") {
                    console.log(`[cashScheduler] ${entry.characterName} is dead, defer use of "${entry.itemName}"`);
                    continue;
                }
                if (statusKind !== "alive") {
                    console.warn(`[cashScheduler] ${entry.characterName} player status is ${statusKind}, skip this tick`);
                    continue;
                }
            } catch (e: any) {
                console.warn(`[cashScheduler] getStatus error ${entry.characterName}: ${e.message} — skip this tick`);
                continue;
            }

            try {
                const bagResult = await this.registry.sendCommand(
                    livePid, "getCashBagItems", {}, 5000
                );
                if (!bagResult.ok) {
                    console.warn(`[cashScheduler] getCashBagItems failed for ${entry.characterName}: ${bagResult.detail}`);
                    continue;
                }
                const items: { slotIndex: number; name: string }[] = JSON.parse(bagResult.detail || "[]");
                const found = items.find((it) => it.name === entry.itemName);
                if (!found) {
                    console.warn(`[cashScheduler] item "${entry.itemName}" not found in ${entry.characterName}'s cash bag`);
                    continue;
                }

                const r = await this.registry.sendCommand(
                    livePid,
                    "useCashItem",
                    { slotIndex: found.slotIndex },
                    5000
                );
                if (r.ok) {
                    entry.lastUsed = now;
                    console.log(
                        `[cashScheduler] used "${entry.itemName}" (slot=${found.slotIndex}) for ${entry.characterName}`
                    );
                } else {
                    console.warn(
                        `[cashScheduler] useCashItem failed for ${entry.characterName}: ${r.detail}`
                    );
                }
            } catch (e: any) {
                console.warn(`[cashScheduler] error ${entry.characterName}: ${e.message}`);
            }
        }
        this.save();
    }

    getEntries(): CashScheduleEntry[] {
        return this.entries;
    }

    getEntriesForCharacter(characterName: string): CashScheduleEntry[] {
        return this.entries.filter((e) => e.characterName === characterName);
    }

    addOrUpdate(entry: Omit<CashScheduleEntry, "lastUsed"> & { lastUsed?: number }): void {
        const idx = this.entries.findIndex(
            (e) => e.characterName === entry.characterName && e.itemName === entry.itemName
        );
        const full: CashScheduleEntry = { ...entry, lastUsed: entry.lastUsed ?? 0 };
        if (idx >= 0) {
            this.entries[idx] = full;
        } else {
            this.entries.push(full);
        }
        this.save();
    }

    remove(characterName: string, itemName: string): boolean {
        const before = this.entries.length;
        this.entries = this.entries.filter(
            (e) => !(e.characterName === characterName && e.itemName === itemName)
        );
        if (this.entries.length !== before) {
            this.save();
            return true;
        }
        return false;
    }

    removeAllForCharacter(characterName: string): void {
        this.entries = this.entries.filter((e) => e.characterName !== characterName);
        this.save();
    }

    destroy(): void {
        if (this.timer) clearInterval(this.timer);
    }
}
