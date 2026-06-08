import fs from "fs/promises";
import os from "os";
import path from "path";
import { InstanceRegistry } from "./instances";

const DEFAULT_ONLINE_SYNC_URL = "http://81.70.213.20:8787";
const CONFIG_FILE_NAME = "online_whitelist_config.json";
const WHITELIST_FILE_NAME = "online_whitelist.json";

export interface OnlineWhitelistConfig {
    enabled: boolean;
    serverUrl: string;
    hostName: string;
    pushIntervalMs: number;
    pullIntervalMs: number;
    whitelistTtlMs: number;
    outputFile: string;
}

interface RemoteOnlineCharacter {
    host?: string;
    name?: string;
    pid?: number;
    accountName?: string;
    windowTitle?: string;
    lastSeen?: number;
    ageMs?: number;
}

interface RemoteOnlineSnapshot {
    ok?: boolean;
    serverTime?: number;
    maxAgeMs?: number;
    characters?: RemoteOnlineCharacter[];
}

export class OnlineWhitelistSync {
    private readonly configFile: string;
    private config: OnlineWhitelistConfig;
    private pushTimer?: NodeJS.Timeout;
    private pullTimer?: NodeJS.Timeout;
    private lastWarnAt = 0;
    private lastPushAt = 0;
    private lastPullAt = 0;
    private lastPullOkAt = 0;
    private lastError = "";

    constructor(private readonly registry: InstanceRegistry, dataDir: string) {
        this.configFile = defaultConfigFile(dataDir);
        this.config = this.defaultConfig(dataDir);
        void this.loadConfig().then(() => this.applyConfig());
    }

    destroy(): void {
        this.stopTimers();
    }

    getSnapshot(): { config: OnlineWhitelistConfig; status: Record<string, unknown> } {
        return {
            config: { ...this.config },
            status: {
                configFile: this.configFile,
                outputFile: this.config.outputFile,
                running: !!(this.pushTimer || this.pullTimer),
                lastPushAt: this.lastPushAt,
                lastPullAt: this.lastPullAt,
                lastPullOkAt: this.lastPullOkAt,
                lastError: this.lastError,
            },
        };
    }

    async readWhitelistFile(): Promise<Record<string, unknown>> {
        try {
            const raw = await fs.readFile(this.config.outputFile, "utf8");
            const parsed = JSON.parse(raw);
            return parsed && typeof parsed === "object" ? parsed : {};
        } catch (e: any) {
            if (e?.code === "ENOENT") return { version: 1, characters: [] };
            return { version: 1, characters: [], error: e.message || String(e) };
        }
    }

    async setConfig(patch: Partial<OnlineWhitelistConfig>): Promise<OnlineWhitelistConfig> {
        this.config = normalizeConfig({ ...this.config, ...patch }, this.config);
        await this.saveConfig();
        this.applyConfig();
        return { ...this.config };
    }

    async triggerSync(): Promise<void> {
        await this.pushOnline();
        await this.pullGlobal();
    }

    private defaultConfig(dataDir: string): OnlineWhitelistConfig {
        return {
            enabled: true,
            serverUrl: process.env.GGTB_ONLINE_SYNC_URL || DEFAULT_ONLINE_SYNC_URL,
            hostName: (process.env.GGTB_ONLINE_SYNC_HOST || os.hostname()).trim() || "unknown-host",
            outputFile: process.env.GGTB_ONLINE_WHITELIST_FILE || defaultWhitelistFile(dataDir),
            pushIntervalMs: readPositiveIntEnv("GGTB_ONLINE_SYNC_PUSH_MS", 10_000),
            pullIntervalMs: readPositiveIntEnv("GGTB_ONLINE_SYNC_PULL_MS", 15_000),
            whitelistTtlMs: readPositiveIntEnv("GGTB_ONLINE_WHITELIST_TTL_MS", 30_000),
        };
    }

    private async loadConfig(): Promise<void> {
        try {
            const raw = await fs.readFile(this.configFile, "utf8");
            const parsed = JSON.parse(raw);
            this.config = normalizeConfig(parsed, this.config);
        } catch (e: any) {
            if (e?.code !== "ENOENT") this.warn(`config read failed: ${e.message || String(e)}`);
            await this.saveConfig();
        }
    }

    private async saveConfig(): Promise<void> {
        const tmp = `${this.configFile}.tmp`;
        await fs.mkdir(path.dirname(this.configFile), { recursive: true });
        await fs.writeFile(tmp, JSON.stringify(this.config, null, 2), "utf8");
        await fs.rename(tmp, this.configFile);
    }

    private applyConfig(): void {
        this.stopTimers();
        if (!this.config.enabled) {
            console.log("[broker] online whitelist sync disabled by config");
            return;
        }

        console.log(`[broker] online whitelist sync: url=${this.config.serverUrl} host=${this.config.hostName}`);
        console.log(`[broker] online whitelist file: ${this.config.outputFile}`);
        this.pushTimer = setInterval(() => void this.pushOnline(), this.config.pushIntervalMs);
        this.pullTimer = setInterval(() => void this.pullGlobal(), this.config.pullIntervalMs);
        void this.triggerSync();
    }

    private stopTimers(): void {
        if (this.pushTimer) clearInterval(this.pushTimer);
        if (this.pullTimer) clearInterval(this.pullTimer);
        this.pushTimer = undefined;
        this.pullTimer = undefined;
    }

    private async pushOnline(): Promise<void> {
        if (!this.config.enabled || !this.config.serverUrl) return;
        const now = Date.now();
        const characters = this.registry.list()
            .filter((inst) => !!inst.characterName && now - inst.lastSeen <= this.config.whitelistTtlMs)
            .map((inst) => ({
                name: inst.characterName,
                pid: inst.pid,
                accountName: inst.accountName,
                windowTitle: inst.windowTitle,
                lastSeen: inst.lastSeen,
            }));

        try {
            await postJson(`${this.config.serverUrl}/api/online`, {
                host: this.config.hostName,
                sentAt: now,
                characters,
            });
            this.lastPushAt = Date.now();
        } catch (e: any) {
            this.warn(`push failed: ${e.message || String(e)}`);
        }
    }

    private async pullGlobal(): Promise<void> {
        if (!this.config.enabled || !this.config.serverUrl) return;
        try {
            const snapshot = await getJson(`${this.config.serverUrl}/api/online?maxAgeMs=${this.config.whitelistTtlMs}`) as RemoteOnlineSnapshot;
            await this.writeWhitelistFile(snapshot);
            this.lastPullAt = Date.now();
            this.lastPullOkAt = this.lastPullAt;
        } catch (e: any) {
            this.warn(`pull failed: ${e.message || String(e)}`);
        }
    }

    private async writeWhitelistFile(snapshot: RemoteOnlineSnapshot): Promise<void> {
        const now = Date.now();
        const chars = Array.isArray(snapshot.characters) ? snapshot.characters : [];
        const characters = chars
            .filter((c) => typeof c.name === "string" && c.name.trim())
            .map((c) => {
                const ageMs = Number.isFinite(Number(c.ageMs)) ? Math.max(0, Number(c.ageMs)) : 0;
                const remainingMs = Math.max(0, this.config.whitelistTtlMs - ageMs);
                return {
                    host: c.host || "",
                    name: String(c.name),
                    pid: Number.isFinite(Number(c.pid)) ? Number(c.pid) : undefined,
                    accountName: c.accountName,
                    windowTitle: c.windowTitle,
                    lastSeen: Number.isFinite(Number(c.lastSeen)) ? Number(c.lastSeen) : undefined,
                    ageMs,
                    expiresAt: now + remainingMs,
                };
            })
            .filter((c) => c.expiresAt > now);

        const payload = {
            version: 1,
            updatedAt: now,
            sourceUrl: this.config.serverUrl,
            hostName: this.config.hostName,
            ttlMs: this.config.whitelistTtlMs,
            expireMs: this.config.whitelistTtlMs,
            characters,
        };

        const tmp = `${this.config.outputFile}.tmp`;
        await fs.mkdir(path.dirname(this.config.outputFile), { recursive: true });
        await fs.writeFile(tmp, JSON.stringify(payload, null, 2), "utf8");
        await fs.rename(tmp, this.config.outputFile);
    }

    private warn(message: string): void {
        this.lastError = message;
        const now = Date.now();
        if (now - this.lastWarnAt < 30_000) return;
        this.lastWarnAt = now;
        console.warn(`[broker] online whitelist sync ${message}`);
    }
}

function normalizeConfig(raw: any, fallback: OnlineWhitelistConfig): OnlineWhitelistConfig {
    return {
        enabled: typeof raw?.enabled === "boolean" ? raw.enabled : fallback.enabled,
        serverUrl: normalizeBaseUrl(stringValue(raw?.serverUrl, fallback.serverUrl)),
        hostName: stringValue(raw?.hostName, fallback.hostName).trim() || fallback.hostName,
        outputFile: stringValue(raw?.outputFile, fallback.outputFile).trim() || fallback.outputFile,
        pushIntervalMs: positiveNumber(raw?.pushIntervalMs, fallback.pushIntervalMs, 1000),
        pullIntervalMs: positiveNumber(raw?.pullIntervalMs, fallback.pullIntervalMs, 1000),
        whitelistTtlMs: positiveNumber(raw?.whitelistTtlMs ?? raw?.expireMs, fallback.whitelistTtlMs, 1000),
    };
}

function normalizeBaseUrl(url: string): string {
    return url.trim().replace(/\/+$/, "");
}

function stringValue(value: unknown, fallback: string): string {
    return typeof value === "string" ? value : fallback;
}

function positiveNumber(value: unknown, fallback: number, min: number): number {
    const n = Number(value);
    return Number.isFinite(n) && n >= min ? Math.floor(n) : fallback;
}

function readPositiveIntEnv(name: string, fallback: number): number {
    const value = Number(process.env[name]);
    return Number.isFinite(value) && value > 0 ? Math.floor(value) : fallback;
}

function defaultConfigFile(dataDir: string): string {
    return path.resolve(machineGgtbDir(dataDir), CONFIG_FILE_NAME);
}

function defaultWhitelistFile(dataDir: string): string {
    return path.resolve(machineGgtbDir(dataDir), WHITELIST_FILE_NAME);
}

function machineGgtbDir(dataDir: string): string {
    const localAppData = process.env.LOCALAPPDATA;
    if (localAppData) return path.resolve(localAppData, "GGTB");
    return dataDir;
}

async function postJson(url: string, body: unknown): Promise<unknown> {
    return requestJson(url, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(body),
    });
}

async function getJson(url: string): Promise<unknown> {
    return requestJson(url, { method: "GET" });
}

async function requestJson(url: string, init: any): Promise<unknown> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 5000);
    try {
        const res = await fetch(url, { ...init, signal: controller.signal });
        const text = await res.text();
        if (!res.ok) throw new Error(`HTTP ${res.status}: ${text.slice(0, 200)}`);
        return text ? JSON.parse(text) : {};
    } finally {
        clearTimeout(timer);
    }
}
