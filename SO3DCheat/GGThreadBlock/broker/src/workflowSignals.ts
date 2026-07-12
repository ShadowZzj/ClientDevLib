export interface TicketProtocolState {
    ticketBranch: boolean;
    scopedAccepted: boolean;
    ticketConsumedConfirmed: boolean;
    firstRegistrationSeq: number;
    continuationSeq: number;
    ticketUpdateSeq: number;
    postTicketRegistrationSeq: number;
}

export function isTicketProtocolReady(state: TicketProtocolState): boolean {
    return state.ticketBranch &&
        state.scopedAccepted &&
        state.ticketConsumedConfirmed &&
        state.continuationSeq > state.firstRegistrationSeq &&
        state.ticketUpdateSeq > state.firstRegistrationSeq &&
        state.postTicketRegistrationSeq > Math.max(state.continuationSeq, state.ticketUpdateSeq);
}

export interface StableMatchObservation {
    sinceMs: number | null;
    complete: boolean;
}

export function observeStableMatch(
    previousSinceMs: number | null,
    matches: boolean,
    nowMs: number,
    stableDurationMs: number
): StableMatchObservation {
    if (!matches) return { sinceMs: null, complete: false };
    const sinceMs = previousSinceMs ?? nowMs;
    return {
        sinceMs,
        complete: nowMs - sinceMs >= stableDurationMs,
    };
}

export type LocalPlayerStatusKind =
    | "alive"
    | "dead"
    | "unavailable"
    | "inconsistent"
    | "disconnecting";

export function classifyLocalPlayerStatus(status: unknown): LocalPlayerStatusKind {
    if (!status || typeof status !== "object" || Array.isArray(status)) return "unavailable";
    const value = status as Record<string, unknown>;
    if (value.clientClosing === true) return "disconnecting";
    if (value.hpKnown === false || value.playerReady === false) return "unavailable";
    if (typeof value.userId === "number" && (!Number.isFinite(value.userId) || value.userId <= 0)) {
        return "unavailable";
    }
    if (typeof value.hp !== "number" || !Number.isFinite(value.hp) || value.hp < 0) {
        return "unavailable";
    }
    if (value.hp === 0 && value.isDead === true) return "dead";
    if (value.hp > 0 && value.isDead === false) return "alive";
    return "inconsistent";
}

export function isCollectionContextStable(
    expectedMapId: number | undefined,
    actualMapId: number | undefined,
    isDead: boolean
): boolean {
    return expectedMapId !== undefined &&
        actualMapId !== undefined &&
        Number.isFinite(expectedMapId) &&
        Number.isFinite(actualMapId) &&
        expectedMapId === actualMapId &&
        !isDead;
}

const PATH_START_NOT_READY = "pathTo failed: not ready (no local player / start tile blocked)";
const LOCAL_POSITION_NOT_READY = "getLocalPosition failed: g_pLocalUser not resolved";

export function isTransientPathStartError(error: unknown): boolean {
    const message = error instanceof Error ? error.message : String(error);
    return message.trim() === PATH_START_NOT_READY;
}

export function isTransientLocalPositionError(error: unknown): boolean {
    const message = error instanceof Error ? error.message : String(error);
    return message.trim() === LOCAL_POSITION_NOT_READY;
}

export function isRecoverableCombatPathError(error: unknown): boolean {
    const message = error instanceof Error ? error.message : String(error);
    const normalized = message.trim();
    return normalized === "path failed: path start or target tile is blocked" ||
        normalized === "path failed: map collision grid not ready" ||
        normalized === "path failed: path planner unavailable" ||
        normalized === "path failed: path planner returned invalid bounds" ||
        normalized === "path failed: no path in full collision grid" ||
        normalized === "path failed: no path within adaptive search bounds" ||
        normalized === "path failed: g_pLocalUser not resolved" ||
        normalized === "path failed: lost local player mid-path" ||
        normalized === "path failed: lost local player mid-segment" ||
        normalized === "path stopped before arrival" ||
        normalized.startsWith("path target was overwritten:") ||
        normalized.startsWith("pathTo start remained not ready for ") ||
        normalized.startsWith("path failed: path search exceeds ") ||
        normalized.startsWith("path failed: stuck") ||
        normalized === "path failed: movement action gate stayed busy" ||
        normalized === "path failed: path planning cancelled";
}

export function isRecoverableCombatSelectionError(error: unknown): boolean {
    const message = error instanceof Error ? error.message : String(error);
    const normalized = message.trim();
    return normalized === "command timed out" ||
        normalized === "selectMonsterForAttack failed: creature is no longer available" ||
        normalized === "selectMonsterForAttack failed: creature is no longer alive" ||
        normalized === "selectMonsterForAttack failed: creature attack position is unreadable" ||
        normalized === "selectMonsterForAttack failed: failed to select the combat target" ||
        normalized === "selectMonsterForAttack failed: combat target changed before selection was confirmed";
}

export interface CombatApproachPoint {
    x: number;
    y: number;
}

export function buildCombatApproachCandidates(
    playerX: unknown,
    playerY: unknown,
    targetX: unknown,
    targetY: unknown,
    approachDistance: unknown
): CombatApproachPoint[] {
    const px = toFiniteNumber(playerX);
    const py = toFiniteNumber(playerY);
    const tx = toFiniteNumber(targetX);
    const ty = toFiniteNumber(targetY);
    const maxDistance = toFiniteNumber(approachDistance);
    if (px === undefined || py === undefined || tx === undefined || ty === undefined ||
        maxDistance === undefined || maxDistance <= 0) {
        return [];
    }

    const baseAngle = Math.atan2(py - ty, px - tx);
    const radius = Math.max(0.25, maxDistance - 0.75);
    const angleOffsets = [
        0,
        Math.PI / 4,
        -Math.PI / 4,
        Math.PI / 2,
        -Math.PI / 2,
        3 * Math.PI / 4,
        -3 * Math.PI / 4,
        Math.PI,
    ];
    const playerTile = `${Math.floor(px)},${Math.floor(py)}`;
    const targetTile = `${Math.floor(tx)},${Math.floor(ty)}`;
    const usedTiles = new Set<string>();
    const candidates: CombatApproachPoint[] = [];
    for (const offset of angleOffsets) {
        const angle = baseAngle + offset;
        const tileX = Math.floor(tx + Math.cos(angle) * radius);
        const tileY = Math.floor(ty + Math.sin(angle) * radius);
        const tileKey = `${tileX},${tileY}`;
        if (tileKey === playerTile || tileKey === targetTile || usedTiles.has(tileKey)) continue;
        const x = tileX + 0.5;
        const y = tileY + 0.5;
        if (Math.hypot(x - tx, y - ty) > maxDistance) continue;
        usedTiles.add(tileKey);
        candidates.push({ x, y });
    }
    return candidates;
}

export function combatNavigationDistance(
    playerX: unknown,
    playerY: unknown,
    targetX: unknown,
    targetY: unknown,
    reportedDistance: unknown
): number | undefined {
    const reported = toFiniteNumber(reportedDistance);
    const px = toFiniteNumber(playerX);
    const py = toFiniteNumber(playerY);
    const tx = toFiniteNumber(targetX);
    const ty = toFiniteNumber(targetY);
    if (px === undefined || py === undefined || tx === undefined || ty === undefined) {
        return reported;
    }

    const planar = Math.hypot(tx - px, ty - py);
    // getStatus and getNearbyNpcs are separate snapshots. If their planar result is
    // much larger than the DLL-reported distance, prefer the internally consistent
    // DLL snapshot instead of chasing a stale player position.
    if (reported !== undefined && planar > reported + 5) return reported;
    return planar;
}

function toFiniteNumber(value: unknown): number | undefined {
    if (value === null || value === undefined || value === "") return undefined;
    const number = Number(value);
    return Number.isFinite(number) ? number : undefined;
}
