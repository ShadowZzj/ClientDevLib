import fs from "fs";
import path from "path";
import { Instance, InstanceRegistry, MoneyUpdateEvent } from "./instances";

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "money_stats.json"
);

const MAX_POINTS_PER_CHARACTER = 90 * 24; // hourly points, roughly 90 days
const MAX_GAIN_EVENTS_PER_CHARACTER = 2000;
const MAX_WALLET_EVENTS_PER_CHARACTER = 5000;
const MAX_REASONABLE_MONEY = 100_000_000_000; // broker-side guard against torn DLL reads
const MAX_REASONABLE_GAIN = 1_000_000_000; // one packet delta should not add billions

interface MoneyPoint {
    bucket: number;
    money: number;
}

interface GainEvent {
    time: number;
    gain: number;
    money: number;
}

interface WalletEvent {
    time: number;
    money: number;
    source?: "status" | "packet";
    proto?: number;
}

interface CharacterStats {
    lastMoney?: number;
    lastSeen?: number;
    lastPacketMoney?: number;
    lastPacketAt?: number;
    hourly: MoneyPoint[];
    dailyGain: Record<string, number>;
    gainEvents: GainEvent[];
    walletEvents: WalletEvent[];
}

interface PersistedDoc {
    characters: Record<string, CharacterStats>;
}

export interface MoneySeriesPoint {
    time: number;
    money: number;
}

export interface MoneyDailyGain {
    date: string;
    gain: number;
}

export interface MoneyGainEvent {
    time: number;
    date: string;
    gain: number;
    money: number;
}

export interface MoneyWalletEvent {
    time: number;
    date: string;
    money: number;
    source?: "status" | "packet";
    proto?: number;
}

export interface MoneyStatsSnapshot {
    characterName: string;
    unit: "hour" | "day";
    series: MoneySeriesPoint[];
    dailyGain: MoneyDailyGain[];
    gainEvents: MoneyGainEvent[];
    walletEvents: MoneyWalletEvent[];
}

function dayKey(ts: number): string {
    const d = new Date(ts);
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${y}-${m}-${day}`;
}

function bucketStart(ts: number, unit: "hour" | "day"): number {
    const d = new Date(ts);
    if (unit === "hour") {
        d.setMinutes(0, 0, 0);
    } else {
        d.setHours(0, 0, 0, 0);
    }
    return d.getTime();
}

function emptyStats(): CharacterStats {
    return { hourly: [], dailyGain: {}, gainEvents: [], walletEvents: [] };
}

export class MoneyStats {
    private characters = new Map<string, CharacterStats>();
    private saveTimer: NodeJS.Timeout | null = null;

    constructor(registry: InstanceRegistry) {
        this.load();
        registry.on("status", (inst: Instance) => this.onStatus(inst));
        registry.on("moneyUpdate", (event: MoneyUpdateEvent) => this.onMoneyUpdate(event));
    }

    private load(): void {
        try {
            if (!fs.existsSync(PERSIST_FILE)) return;
            const raw = fs.readFileSync(PERSIST_FILE, "utf8");
            const doc = JSON.parse(raw) as PersistedDoc;
            let migrated = false;
            for (const [name, stats] of Object.entries(doc.characters || {})) {
                const normalized = normalizeLoadedStats(stats);
                migrated = migrated || normalized.migrated;
                this.characters.set(name, normalized.stats);
            }
            console.log(`[moneyStats] loaded ${this.characters.size} characters`);
            if (migrated) {
                this.save();
                console.log("[moneyStats] migrated persisted money stats");
            }
        } catch (e: any) {
            console.warn(`[moneyStats] load failed: ${e.message}`);
            this.characters.clear();
        }
    }

    private scheduleSave(): void {
        if (this.saveTimer) return;
        this.saveTimer = setTimeout(() => {
            this.saveTimer = null;
            this.save();
        }, 2000);
    }

    private save(): void {
        const characters: Record<string, CharacterStats> = {};
        for (const [name, stats] of this.characters) characters[name] = stats;
        try {
            fs.writeFileSync(PERSIST_FILE, JSON.stringify({ characters }, null, 2), "utf8");
        } catch (e: any) {
            console.warn(`[moneyStats] save failed: ${e.message}`);
        }
    }

    private onStatus(inst: Instance): void {
        const name = inst.characterName;
        const money = inst.status.money;
        if (!name || !isReliableMoney(money)) {
            if (name && typeof money === "number" && Number.isFinite(money)) {
                console.warn(`[moneyStats] ignored suspicious money name=${name} money=${money}`);
            }
            return;
        }

        const now = Date.now();
        const stats = this.characters.get(name) || emptyStats();
        if (typeof stats.lastMoney !== "number" || money !== stats.lastMoney) {
            this.pushWalletEvent(stats, now, money, "status");
        }
        stats.lastMoney = money;
        stats.lastSeen = now;
        this.upsertHourly(stats, now, money);
        this.characters.set(name, stats);
        this.scheduleSave();
    }

    private onMoneyUpdate(event: MoneyUpdateEvent): void {
        const name = event.characterName;
        const money = event.money;
        if (!name || !isReliableMoney(money)) {
            if (name && typeof money === "number" && Number.isFinite(money)) {
                console.warn(`[moneyStats] ignored suspicious packet money name=${name} money=${money}`);
            }
            return;
        }

        const ts = event.timestamp || Date.now();
        const stats = this.characters.get(name) || emptyStats();
        if (typeof stats.lastPacketMoney !== "number" || money !== stats.lastPacketMoney) {
            this.pushWalletEvent(stats, ts, money, "packet", event.proto);
        }

        if (typeof stats.lastPacketMoney === "number" && money > stats.lastPacketMoney) {
            const gain = money - stats.lastPacketMoney;
            if (isReliableGain(gain)) {
                const key = dayKey(ts);
                stats.dailyGain[key] = (stats.dailyGain[key] || 0) + gain;
                stats.gainEvents.push({ time: ts, gain, money });
                if (stats.gainEvents.length > MAX_GAIN_EVENTS_PER_CHARACTER) {
                    stats.gainEvents.splice(0, stats.gainEvents.length - MAX_GAIN_EVENTS_PER_CHARACTER);
                }
            } else {
                console.warn(`[moneyStats] ignored suspicious packet gain name=${name} prev=${stats.lastPacketMoney} money=${money} gain=${gain}`);
            }
        }

        stats.lastPacketMoney = money;
        stats.lastPacketAt = ts;
        stats.lastMoney = money;
        stats.lastSeen = ts;
        this.upsertHourly(stats, ts, money);
        this.characters.set(name, stats);
        this.scheduleSave();
    }

    private upsertHourly(stats: CharacterStats, ts: number, money: number): void {
        const bucket = bucketStart(ts, "hour");
        const last = stats.hourly[stats.hourly.length - 1];
        if (last && last.bucket === bucket) {
            last.money = money;
        } else {
            stats.hourly.push({ bucket, money });
            if (stats.hourly.length > MAX_POINTS_PER_CHARACTER) {
                stats.hourly.splice(0, stats.hourly.length - MAX_POINTS_PER_CHARACTER);
            }
        }
    }

    private pushWalletEvent(
        stats: CharacterStats,
        ts: number,
        money: number,
        source: "status" | "packet",
        proto?: number
    ): void {
        const event: WalletEvent = { time: ts, money, source };
        if (typeof proto === "number") event.proto = proto;
        stats.walletEvents.push(event);
        if (stats.walletEvents.length > MAX_WALLET_EVENTS_PER_CHARACTER) {
            stats.walletEvents.splice(0, stats.walletEvents.length - MAX_WALLET_EVENTS_PER_CHARACTER);
        }
    }

    listCharacters(): string[] {
        return Array.from(this.characters.keys()).sort();
    }

    snapshot(characterName: string, unit: "hour" | "day" = "hour"): MoneyStatsSnapshot {
        const stats = this.characters.get(characterName) || emptyStats();
        const series = unit === "hour" ? this.hourlySeries(stats) : this.dailySeries(stats);
        const dailyGain = Object.entries(stats.dailyGain)
            .sort(([a], [b]) => a.localeCompare(b))
            .map(([date, gain]) => ({ date, gain }));
        return {
            characterName,
            unit,
            series,
            dailyGain,
            gainEvents: this.gainEvents(stats),
            walletEvents: this.walletEvents(stats),
        };
    }

    allDailyGain(): Record<string, MoneyDailyGain[]> {
        const out: Record<string, MoneyDailyGain[]> = {};
        for (const name of this.listCharacters()) {
            out[name] = Object.entries(this.characters.get(name)?.dailyGain || {})
                .sort(([a], [b]) => a.localeCompare(b))
                .map(([date, gain]) => ({ date, gain }));
        }
        return out;
    }

    private hourlySeries(stats: CharacterStats): MoneySeriesPoint[] {
        return stats.hourly.map((p) => ({ time: p.bucket, money: p.money }));
    }

    private dailySeries(stats: CharacterStats): MoneySeriesPoint[] {
        const byDay = new Map<number, number>();
        for (const p of stats.hourly) {
            byDay.set(bucketStart(p.bucket, "day"), p.money);
        }
        return Array.from(byDay.entries())
            .sort(([a], [b]) => a - b)
            .map(([time, money]) => ({ time, money }));
    }

    private gainEvents(stats: CharacterStats): MoneyGainEvent[] {
        return stats.gainEvents
            .slice()
            .sort((a, b) => a.time - b.time)
            .map((event) => ({
                time: event.time,
                date: formatDateTime(event.time),
                gain: event.gain,
                money: event.money,
            }));
    }

    private walletEvents(stats: CharacterStats): MoneyWalletEvent[] {
        return stats.walletEvents
            .slice()
            .sort((a, b) => a.time - b.time)
            .map((event) => ({
                time: event.time,
                date: formatDateTime(event.time),
                money: event.money,
                source: event.source,
                proto: event.proto,
            }));
    }

    destroy(): void {
        if (this.saveTimer) {
            clearTimeout(this.saveTimer);
            this.saveTimer = null;
        }
        this.save();
    }
}

function isGainEvent(value: any): value is GainEvent {
    return value &&
        typeof value.time === "number" &&
        Number.isFinite(value.time) &&
        typeof value.gain === "number" &&
        Number.isFinite(value.gain) &&
        typeof value.money === "number" &&
        Number.isFinite(value.money);
}

function isWalletEvent(value: any): value is WalletEvent {
    const source = value?.source;
    return value &&
        typeof value.time === "number" &&
        Number.isFinite(value.time) &&
        typeof value.money === "number" &&
        Number.isFinite(value.money) &&
        (source === undefined || source === "status" || source === "packet");
}

function normalizeLoadedStats(input: CharacterStats): { stats: CharacterStats; migrated: boolean } {
    const lastSeen = typeof input.lastSeen === "number" && Number.isFinite(input.lastSeen)
        ? input.lastSeen
        : undefined;
    const inputHourly = Array.isArray(input.hourly) ? input.hourly : [];
    const inputWalletEvents = Array.isArray((input as any).walletEvents)
        ? (input as any).walletEvents
        : [];
    const walletEvents = inputWalletEvents
        .filter(isWalletEvent)
        .filter((event: WalletEvent) => isReliableMoney(event.money))
        .map((event: WalletEvent) => normalizeWalletEvent(event))
        .sort((a: WalletEvent, b: WalletEvent) => a.time - b.time);

    if (walletEvents.length === 0) {
        for (const point of inputHourly) {
            if (isWalletEvent({ time: point.bucket, money: point.money }) && isReliableMoney(point.money)) {
                walletEvents.push({ time: point.bucket, money: point.money, source: "status" });
            }
        }
    }

    const loadedLastMoney = typeof input.lastMoney === "number" && isReliableMoney(input.lastMoney)
        ? input.lastMoney
        : undefined;
    if (typeof loadedLastMoney === "number" && typeof lastSeen === "number") {
        const last = walletEvents[walletEvents.length - 1];
        if (!last || last.time !== lastSeen || last.money !== loadedLastMoney) {
            walletEvents.push({ time: lastSeen, money: loadedLastMoney, source: "status" });
        }
    }

    const dedupedWalletEvents = dedupeWalletEvents(walletEvents).slice(-MAX_WALLET_EVENTS_PER_CHARACTER);
    const rebuilt = rebuildGainStats(dedupedWalletEvents);
    const hourly = rebuildHourly(inputHourly, dedupedWalletEvents);
    const lastWallet = dedupedWalletEvents[dedupedWalletEvents.length - 1];
    const lastMoney = lastWallet?.money ?? loadedLastMoney;
    const normalized: CharacterStats = {
        lastMoney,
        lastSeen,
        lastPacketMoney: rebuilt.lastPacketMoney,
        lastPacketAt: rebuilt.lastPacketAt,
        hourly,
        dailyGain: rebuilt.dailyGain,
        gainEvents: rebuilt.gainEvents,
        walletEvents: dedupedWalletEvents,
    };

    const oldDailyGain = input.dailyGain && typeof input.dailyGain === "object" ? input.dailyGain : {};
    const oldGainEvents = Array.isArray((input as any).gainEvents)
        ? (input as any).gainEvents.filter(isGainEvent)
        : [];
    const migrated =
        inputWalletEvents.length !== dedupedWalletEvents.length ||
        inputHourly.length !== hourly.length ||
        JSON.stringify(oldDailyGain) !== JSON.stringify(normalized.dailyGain) ||
        oldGainEvents.length !== normalized.gainEvents.length ||
        (input as any).lastPacketMoney !== normalized.lastPacketMoney ||
        (input as any).lastPacketAt !== normalized.lastPacketAt ||
        input.lastMoney !== normalized.lastMoney;
    return { stats: normalized, migrated };
}

function rebuildHourly(originalHourly: MoneyPoint[], walletEvents: WalletEvent[]): MoneyPoint[] {
    const byBucket = new Map<number, number>();
    for (const point of originalHourly) {
        if (!isWalletEvent({ time: point.bucket, money: point.money }) || !isReliableMoney(point.money)) continue;
        byBucket.set(point.bucket, point.money);
    }
    for (const event of walletEvents) {
        byBucket.set(bucketStart(event.time, "hour"), event.money);
    }
    return Array.from(byBucket.entries())
        .sort(([a], [b]) => a - b)
        .slice(-MAX_POINTS_PER_CHARACTER)
        .map(([bucket, money]) => ({ bucket, money }));
}

function rebuildGainStats(walletEvents: WalletEvent[]): {
    dailyGain: Record<string, number>;
    gainEvents: GainEvent[];
    lastPacketMoney?: number;
    lastPacketAt?: number;
} {
    const dailyGain: Record<string, number> = {};
    const gainEvents: GainEvent[] = [];
    let packetMoney: number | undefined;
    let packetAt: number | undefined;
    for (const event of walletEvents) {
        if (event.source !== "packet") continue;

        if (typeof packetMoney !== "number" || typeof packetAt !== "number") {
            packetMoney = event.money;
            packetAt = event.time;
            continue;
        }

        if (event.money > packetMoney) {
            const gain = event.money - packetMoney;
            if (isReliableGain(gain)) {
                const key = dayKey(event.time);
                dailyGain[key] = (dailyGain[key] || 0) + gain;
                gainEvents.push({ time: event.time, gain, money: event.money });
            }
        }
        packetMoney = event.money;
        packetAt = event.time;
    }
    return {
        dailyGain,
        gainEvents: gainEvents.slice(-MAX_GAIN_EVENTS_PER_CHARACTER),
        lastPacketMoney: packetMoney,
        lastPacketAt: packetAt,
    };
}

function dedupeWalletEvents(events: WalletEvent[]): WalletEvent[] {
    const out: WalletEvent[] = [];
    for (const event of events) {
        const last = out[out.length - 1];
        if (last &&
            last.time === event.time &&
            last.money === event.money &&
            last.source === event.source &&
            last.proto === event.proto) continue;
        out.push(event);
    }
    return out;
}

function normalizeWalletEvent(event: WalletEvent): WalletEvent {
    const normalized: WalletEvent = {
        time: event.time,
        money: event.money,
        source: event.source === "packet" ? "packet" : event.source === "status" ? "status" : undefined,
    };
    if (typeof event.proto === "number" && Number.isFinite(event.proto)) {
        normalized.proto = event.proto;
    }
    return normalized;
}

function isReliableMoney(value: unknown): value is number {
    return typeof value === "number" &&
        Number.isFinite(value) &&
        Number.isSafeInteger(value) &&
        value > 0 &&
        value <= MAX_REASONABLE_MONEY;
}

function isReliableGain(value: number): boolean {
    return Number.isSafeInteger(value) && value > 0 && value <= MAX_REASONABLE_GAIN;
}

function formatDateTime(ts: number): string {
    const d = new Date(ts);
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    const hh = String(d.getHours()).padStart(2, "0");
    const mm = String(d.getMinutes()).padStart(2, "0");
    const ss = String(d.getSeconds()).padStart(2, "0");
    return `${y}-${m}-${day} ${hh}:${mm}:${ss}`;
}
