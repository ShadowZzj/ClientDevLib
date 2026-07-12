import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import test from "node:test";
import {
    buildCombatApproachCandidates,
    classifyLocalPlayerStatus,
    combatNavigationDistance,
    isRecoverableCombatPathError,
    isRecoverableCombatSelectionError,
    isCollectionContextStable,
    isTicketProtocolReady,
    isTransientLocalPositionError,
    isTransientPathStartError,
    observeStableMatch,
} from "./workflowSignals";

test("local player status distinguishes death from unavailable teardown state", () => {
    assert.equal(classifyLocalPlayerStatus({
        hp: 20100, hpKnown: true, playerReady: true, userId: 1, isDead: false,
    }), "alive");
    assert.equal(classifyLocalPlayerStatus({
        hp: 0, hpKnown: true, playerReady: true, userId: 1, isDead: true,
    }), "dead");
    assert.equal(classifyLocalPlayerStatus({
        hp: -1, hpKnown: false, playerReady: false, userId: 0, isDead: false,
    }), "unavailable");
    assert.equal(classifyLocalPlayerStatus({
        hp: 20100, hpKnown: true, playerReady: true, userId: 1, isDead: false, clientClosing: true,
    }), "disconnecting");
});

test("legacy contradictory or teardown snapshots are never classified as death", () => {
    assert.equal(classifyLocalPlayerStatus({ hp: 20100, isDead: true, userId: 1 }), "inconsistent");
    assert.equal(classifyLocalPlayerStatus({ hp: 0, isDead: false, userId: 1 }), "inconsistent");
    assert.equal(classifyLocalPlayerStatus({ hp: 0, isDead: true, userId: 0 }), "unavailable");
    assert.equal(classifyLocalPlayerStatus({ hp: 0, isDead: true, userId: 1 }), "dead");
});

const readyBase = {
    ticketBranch: true,
    scopedAccepted: true,
    ticketConsumedConfirmed: true,
    firstRegistrationSeq: 1,
    continuationSeq: 2,
    ticketUpdateSeq: 3,
    postTicketRegistrationSeq: 4,
};

test("ticket protocol accepts the observed continuation-before-ack order", () => {
    assert.equal(isTicketProtocolReady(readyBase), true);
});

test("ticket protocol accepts ack-before-continuation when final registration follows both", () => {
    assert.equal(isTicketProtocolReady({
        ...readyBase,
        continuationSeq: 3,
        ticketUpdateSeq: 2,
    }), true);
});

test("ticket protocol rejects a final registration that precedes the ticket ack", () => {
    assert.equal(isTicketProtocolReady({
        ...readyBase,
        ticketUpdateSeq: 4,
        postTicketRegistrationSeq: 3,
    }), false);
});

test("ticket protocol requires scoped acceptance and both ordered signals", () => {
    assert.equal(isTicketProtocolReady({ ...readyBase, scopedAccepted: false }), false);
    assert.equal(isTicketProtocolReady({ ...readyBase, continuationSeq: 0 }), false);
    assert.equal(isTicketProtocolReady({ ...readyBase, ticketUpdateSeq: 0 }), false);
});

test("stable map observation completes only after a continuous match", () => {
    const first = observeStableMatch(null, true, 1000, 1500);
    assert.deepEqual(first, { sinceMs: 1000, complete: false });
    assert.deepEqual(observeStableMatch(first.sinceMs, true, 2499, 1500), {
        sinceMs: 1000,
        complete: false,
    });
    assert.deepEqual(observeStableMatch(first.sinceMs, true, 2500, 1500), {
        sinceMs: 1000,
        complete: true,
    });
    assert.deepEqual(observeStableMatch(first.sinceMs, false, 2000, 1500), {
        sinceMs: null,
        complete: false,
    });
});

test("collection failures are skippable only while alive on the same known map", () => {
    assert.equal(isCollectionContextStable(214, 214, false), true);
    assert.equal(isCollectionContextStable(214, 214, true), false);
    assert.equal(isCollectionContextStable(214, 215, false), false);
    assert.equal(isCollectionContextStable(214, undefined, false), false);
});

test("combat navigation uses planar distance and ignores a transient Z-axis jump", () => {
    assert.equal(combatNavigationDistance(473, 72, 473, 72, 482.5), 0);
    assert.equal(combatNavigationDistance(470, 68, 473, 72, 482.5), 5);
    assert.equal(combatNavigationDistance(undefined, 72, 473, 72, 7), 7);
});

test("combat navigation rejects a stale player-position snapshot", () => {
    assert.equal(combatNavigationDistance(0, 0, 473, 72, 2), 2);
});

test("only the precise path-start readiness rejection is transient", () => {
    assert.equal(isTransientPathStartError(
        new Error("pathTo failed: not ready (no local player / start tile blocked)")
    ), true);
    assert.equal(isTransientPathStartError(new Error("path failed: no route")), false);
    assert.equal(isTransientPathStartError(new Error("pathTo failed: send failed")), false);
});

test("only the unresolved local-player position read is transient", () => {
    assert.equal(isTransientLocalPositionError(
        new Error("getLocalPosition failed: g_pLocalUser not resolved")
    ), true);
    assert.equal(isTransientLocalPositionError(new Error("getLocalPosition timed out")), false);
});

test("combat approach candidates stay on unique nearby tiles and prefer the player side", () => {
    const candidates = buildCombatApproachCandidates(312, 35, 300, 44, 3);
    assert.ok(candidates.length >= 6);
    const tileKeys = candidates.map((point) => `${Math.floor(point.x)},${Math.floor(point.y)}`);
    assert.equal(new Set(tileKeys).size, candidates.length);
    assert.equal(tileKeys.includes("312,35"), false);
    assert.equal(tileKeys.includes("300,44"), false);
    for (const point of candidates) {
        assert.ok(Math.hypot(point.x - 300, point.y - 44) <= 3);
    }
    assert.ok(Math.hypot(candidates[0].x - 312, candidates[0].y - 35) < 15);
});

test("combat approach candidates reject invalid inputs", () => {
    assert.deepEqual(buildCombatApproachCandidates(undefined, 35, 300, 44, 3), []);
    assert.deepEqual(buildCombatApproachCandidates(312, 35, 300, 44, 0), []);
});

test("combat path recovery covers planner and local movement failures", () => {
    assert.equal(isRecoverableCombatPathError(
        new Error("path failed: path start or target tile is blocked")
    ), true);
    assert.equal(isRecoverableCombatPathError(
        new Error("path failed: no path in full collision grid")
    ), true);
    assert.equal(isRecoverableCombatPathError(
        new Error("path failed: stuck — engine won't advance toward goal")
    ), true);
    assert.equal(isRecoverableCombatPathError(
        new Error("path target was overwritten: expected (1, 2), actual (3, 4)")
    ), true);
    assert.equal(isRecoverableCombatPathError(
        new Error("path failed: lost local player mid-path")
    ), true);
    assert.equal(isRecoverableCombatPathError(
        new Error("pathTo start remained not ready for 10000ms (local player or collision map unavailable)")
    ), true);
    assert.equal(isRecoverableCombatPathError(
        new Error("pathStatus missing targetX/targetY")
    ), false);
});

test("combat selection retries transient engine races but rejects capability failures", () => {
    assert.equal(isRecoverableCombatSelectionError(
        new Error("selectMonsterForAttack failed: creature attack position is unreadable")
    ), true);
    assert.equal(isRecoverableCombatSelectionError(
        new Error("selectMonsterForAttack failed: combat target changed before selection was confirmed")
    ), true);
    assert.equal(isRecoverableCombatSelectionError(new Error("command timed out")), true);
    assert.equal(isRecoverableCombatSelectionError(
        new Error("selectMonsterForAttack failed: combat target patterns are unresolved")
    ), false);
    assert.equal(isRecoverableCombatSelectionError(
        new Error("selectMonsterForAttack did not confirm creature 123")
    ), false);
});

test("all pickup filter probes continue into their collection node on failure", () => {
    const workflowFile = path.resolve(__dirname, "../release/workflow_scripts.json");
    const document = JSON.parse(fs.readFileSync(workflowFile, "utf8"));
    const nodes = document.workflows.flatMap((workflow: { nodes: unknown[] }) => workflow.nodes);
    const byId = new Map<string, any>(nodes.map((node: any) => [node.id, node]));
    const collectionNodes = nodes.filter((node: any) => node.type === "collectFilteredDrops");
    const filterProbes = nodes.filter((node: any) =>
        node.type === "command" && node.params?.action === "getPickupFilterStatus");

    assert.equal(collectionNodes.length, 5);
    assert.equal(filterProbes.length, collectionNodes.length);
    for (const probe of filterProbes) {
        assert.equal(probe.onFailure, probe.next);
        assert.equal(byId.get(probe.next)?.type, "collectFilteredDrops");
    }
});

test("tower startup map branch requires a stable observation", () => {
    const workflowFile = path.resolve(__dirname, "../release/workflow_scripts.json");
    const document = JSON.parse(fs.readFileSync(workflowFile, "utf8"));
    const workflow = document.workflows.find((entry: any) => entry.id === "sage-tower-another");
    const node = workflow.nodes.find((entry: any) => entry.id === "already_in_tower");
    assert.equal(node.type, "condition");
    assert.equal(node.params.pollMs, 300);
    assert.equal(node.params.stableDurationMs, 1000);
});
