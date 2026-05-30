import fs from "fs";
import path from "path";
import { AutoReviver, AutoReviveCompletedEvent } from "./autoReviver";

const PERSIST_FILE = path.resolve(
    process.env.GGTB_DATA_DIR || path.dirname(process.execPath),
    "auto_revive_stats.json"
);

const MAX_HOURLY_POINTS_PER_CHARACTER = 90 * 24;
const MAX_DAILY_POINTS_PER_CHARACTER = 180;

interface CountPoint {
    bucket: number;
    count: number;
}

interface CharacterStats {
    hourly: CountPoint[];
    daily: CountPoint[];
}

interface PersistedDoc {
    characters: Record<string, CharacterStats>;
}

export interface AutoReviveCountPoint {
    time: number;
    count: number;
}

export interface AutoReviveDailyCount {
    date: string;
    count: number;
}

export interface AutoReviveStatsSnapshot {
    characterName: string;
    unit: "hour" | "day";
    series: AutoReviveCountPoint[];
    daily: AutoReviveDailyCount[];
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

function dayKey(ts: number): string {
    const d = new Date(ts);
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2, "0");
    const day = String(d.getDate()).padStart(2, "0");
    return `${y}-${m}-${day}`;
}

function emptyStats(): CharacterStats {
    return { hourly: [], daily: [] };
}

function normalizePoints(value: unknown): CountPoint[] {
    if (!Array.isArray(value)) return [];
    return value
        .filter((p) =>
            p &&
            typeof p.bucket === "number" &&
            Number.isFinite(p.bucket) &&
            typeof p.count === "number" &&
            Number.isFinite(p.count)
        )
        .map((p) => ({ bucket: p.bucket, count: Math.max(0, Math.round(p.count)) }))
        .sort((a, b) => a.bucket - b.bucket);
}

export class AutoReviveStats {
    private characters = new Map<string, CharacterStats>();
    private saveTimer: NodeJS.Timeout | null = null;

    constructor(autoReviver: AutoReviver) {
        this.load();
        autoReviver.on("autoRunComplete", (event: AutoReviveCompletedEvent) => this.record(event));
    }

    private load(): void {
        try {
            if (!fs.existsSync(PERSIST_FILE)) return;
            const raw = fs.readFileSync(PERSIST_FILE, "utf8");
            const doc = JSON.parse(raw) as PersistedDoc;
            for (const [name, stats] of Object.entries(doc.characters || {})) {
                this.characters.set(name, {
                    hourly: normalizePoints(stats.hourly),
                    daily: normalizePoints(stats.daily),
                });
            }
            console.log(`[autoReviveStats] loaded ${this.characters.size} characters`);
        } catch (e: any) {
            console.warn(`[autoReviveStats] load failed: ${e.message}`);
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
            console.warn(`[autoReviveStats] save failed: ${e.message}`);
        }
    }

    private record(event: AutoReviveCompletedEvent): void {
        const name = event.characterName;
        if (!name) return;
        const stats = this.characters.get(name) || emptyStats();
        this.increment(stats.hourly, bucketStart(event.at, "hour"), MAX_HOURLY_POINTS_PER_CHARACTER);
        this.increment(stats.daily, bucketStart(event.at, "day"), MAX_DAILY_POINTS_PER_CHARACTER);
        this.characters.set(name, stats);
        this.scheduleSave();
    }

    private increment(points: CountPoint[], bucket: number, maxPoints: number): void {
        const last = points[points.length - 1];
        if (last && last.bucket === bucket) {
            last.count += 1;
        } else {
            points.push({ bucket, count: 1 });
            if (points.length > maxPoints) {
                points.splice(0, points.length - maxPoints);
            }
        }
    }

    listCharacters(): string[] {
        return Array.from(this.characters.keys()).sort();
    }

    snapshot(characterName: string, unit: "hour" | "day" = "hour"): AutoReviveStatsSnapshot {
        const stats = this.characters.get(characterName) || emptyStats();
        const source = unit === "hour" ? stats.hourly : stats.daily;
        return {
            characterName,
            unit,
            series: source.map((p) => ({ time: p.bucket, count: p.count })),
            daily: stats.daily.map((p) => ({ date: dayKey(p.bucket), count: p.count })),
        };
    }

    allDaily(): Record<string, AutoReviveDailyCount[]> {
        const out: Record<string, AutoReviveDailyCount[]> = {};
        for (const name of this.listCharacters()) {
            out[name] = this.characters.get(name)!.daily.map((p) => ({ date: dayKey(p.bucket), count: p.count }));
        }
        return out;
    }

    destroy(): void {
        if (this.saveTimer) {
            clearTimeout(this.saveTimer);
            this.saveTimer = null;
        }
        this.save();
    }
}
