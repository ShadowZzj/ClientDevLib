import fs from "fs";
import path from "path";
import { Instance, InstanceRegistry } from "./instances";

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "money_stats.json"
);

const MAX_POINTS_PER_CHARACTER = 90 * 24; // hourly points, roughly 90 days

interface MoneyPoint {
    bucket: number;
    money: number;
}

interface CharacterStats {
    lastMoney?: number;
    lastSeen?: number;
    hourly: MoneyPoint[];
    dailyGain: Record<string, number>;
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

export interface MoneyStatsSnapshot {
    characterName: string;
    unit: "hour" | "day";
    series: MoneySeriesPoint[];
    dailyGain: MoneyDailyGain[];
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
    return { hourly: [], dailyGain: {} };
}

export class MoneyStats {
    private characters = new Map<string, CharacterStats>();
    private saveTimer: NodeJS.Timeout | null = null;

    constructor(registry: InstanceRegistry) {
        this.load();
        registry.on("status", (inst: Instance) => this.onStatus(inst));
    }

    private load(): void {
        try {
            if (!fs.existsSync(PERSIST_FILE)) return;
            const raw = fs.readFileSync(PERSIST_FILE, "utf8");
            const doc = JSON.parse(raw) as PersistedDoc;
            for (const [name, stats] of Object.entries(doc.characters || {})) {
                this.characters.set(name, {
                    lastMoney: typeof stats.lastMoney === "number" ? stats.lastMoney : undefined,
                    lastSeen: typeof stats.lastSeen === "number" ? stats.lastSeen : undefined,
                    hourly: Array.isArray(stats.hourly) ? stats.hourly : [],
                    dailyGain: stats.dailyGain && typeof stats.dailyGain === "object" ? stats.dailyGain : {},
                });
            }
            console.log(`[moneyStats] loaded ${this.characters.size} characters`);
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
        if (!name || typeof money !== "number" || !Number.isFinite(money)) return;

        const now = Date.now();
        const stats = this.characters.get(name) || emptyStats();
        const prev = stats.lastMoney;
        if (typeof prev === "number" && money > prev) {
            const gain = money - prev;
            const key = dayKey(now);
            stats.dailyGain[key] = (stats.dailyGain[key] || 0) + gain;
        }
        stats.lastMoney = money;
        stats.lastSeen = now;
        this.upsertHourly(stats, now, money);
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

    listCharacters(): string[] {
        return Array.from(this.characters.keys()).sort();
    }

    snapshot(characterName: string, unit: "hour" | "day" = "hour"): MoneyStatsSnapshot {
        const stats = this.characters.get(characterName) || emptyStats();
        const series = unit === "hour" ? this.hourlySeries(stats) : this.dailySeries(stats);
        const dailyGain = Object.entries(stats.dailyGain)
            .sort(([a], [b]) => a.localeCompare(b))
            .map(([date, gain]) => ({ date, gain }));
        return { characterName, unit, series, dailyGain };
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

    destroy(): void {
        if (this.saveTimer) {
            clearTimeout(this.saveTimer);
            this.saveTimer = null;
        }
        this.save();
    }
}
