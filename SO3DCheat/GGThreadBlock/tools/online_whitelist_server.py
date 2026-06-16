#!/usr/bin/env python3
"""Central online-character registry for GGThreadBlock brokers.

Each broker POSTs its currently online characters to /api/online. Brokers also
poll /api/online and write the returned global list to a local JSON file that
GGThreadBlock can use as a short-lived cross-host whitelist.
"""

from __future__ import annotations

import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import RLock
from typing import Any
from urllib.parse import parse_qs, urlparse


def now_ms() -> int:
    return int(time.time() * 1000)


class OnlineRegistry:
    def __init__(self, retention_ms: int) -> None:
        self.retention_ms = retention_ms
        self._lock = RLock()
        self._hosts: dict[str, dict[str, Any]] = {}

    def update_host(self, host: str, characters: list[dict[str, Any]]) -> dict[str, Any]:
        t = now_ms()
        clean_host = str(host or "").strip()[:128]
        if not clean_host:
            raise ValueError("missing host")

        clean_chars: list[dict[str, Any]] = []
        seen: set[str] = set()
        for item in characters:
            if not isinstance(item, dict):
                continue
            name = str(item.get("name") or item.get("characterName") or "").strip()
            if not name or name in seen:
                continue
            seen.add(name)
            clean_chars.append(
                {
                    "name": name,
                    "pid": item.get("pid"),
                    "accountName": item.get("accountName"),
                    "windowTitle": item.get("windowTitle"),
                    "brokerLastSeen": item.get("lastSeen"),
                    "lastSeen": t,
                }
            )

        with self._lock:
            self._prune_locked(t)
            self._hosts[clean_host] = {"host": clean_host, "lastSeen": t, "characters": clean_chars}
            return self.snapshot(max_age_ms=self.retention_ms, now=t)

    def snapshot(self, max_age_ms: int, now: int | None = None) -> dict[str, Any]:
        t = now_ms() if now is None else now
        max_age = max(1000, int(max_age_ms))
        with self._lock:
            self._prune_locked(t)
            hosts: dict[str, Any] = {}
            flat: list[dict[str, Any]] = []
            for host, host_entry in sorted(self._hosts.items()):
                host_chars: list[dict[str, Any]] = []
                for char in host_entry.get("characters", []):
                    last_seen = int(char.get("lastSeen") or 0)
                    age_ms = t - last_seen
                    if age_ms < 0 or age_ms > max_age:
                        continue
                    row = {
                        "host": host,
                        "name": char.get("name"),
                        "pid": char.get("pid"),
                        "accountName": char.get("accountName"),
                        "windowTitle": char.get("windowTitle"),
                        "lastSeen": last_seen,
                        "ageMs": age_ms,
                    }
                    host_chars.append(row)
                    flat.append(row)
                hosts[host] = {
                    "host": host,
                    "lastSeen": host_entry.get("lastSeen"),
                    "ageMs": t - int(host_entry.get("lastSeen") or 0),
                    "characters": host_chars,
                }
            flat.sort(key=lambda x: (str(x.get("host") or ""), str(x.get("name") or "")))
            return {
                "ok": True,
                "serverTime": t,
                "maxAgeMs": max_age,
                "hosts": hosts,
                "characters": flat,
            }

    def _prune_locked(self, t: int) -> None:
        cutoff = t - self.retention_ms
        stale_hosts = [
            host for host, entry in self._hosts.items()
            if int(entry.get("lastSeen") or 0) < cutoff
        ]
        for host in stale_hosts:
            del self._hosts[host]


def make_handler(registry: OnlineRegistry):
    class Handler(BaseHTTPRequestHandler):
        server_version = "GGTBOnlineWhitelist/1.0"

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path in ("/", "/health", "/api/health"):
                self._json(200, {"ok": True, "time": now_ms()})
                return
            if parsed.path == "/api/online":
                qs = parse_qs(parsed.query)
                max_age_ms = int((qs.get("maxAgeMs") or [registry.retention_ms])[0])
                self._json(200, registry.snapshot(max_age_ms=max_age_ms))
                return
            self._json(404, {"ok": False, "error": "not found"})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            if parsed.path != "/api/online":
                self._json(404, {"ok": False, "error": "not found"})
                return
            try:
                body = self._read_json()
                result = registry.update_host(
                    str(body.get("host") or body.get("hostName") or ""),
                    body.get("characters") if isinstance(body.get("characters"), list) else [],
                )
                self._json(200, result)
            except Exception as exc:
                self._json(400, {"ok": False, "error": str(exc)})

        def log_message(self, fmt: str, *args: Any) -> None:
            print(f"{time.strftime('%Y-%m-%d %H:%M:%S')} | {self.address_string()} | {fmt % args}")

        def _read_json(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length") or 0)
            raw = self.rfile.read(length) if length > 0 else b"{}"
            if not raw:
                return {}
            data = json.loads(raw.decode("utf-8"))
            if not isinstance(data, dict):
                raise ValueError("body must be a JSON object")
            return data

        def _json(self, status: int, data: dict[str, Any]) -> None:
            payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(payload)

    return Handler


def main() -> None:
    parser = argparse.ArgumentParser(description="GGThreadBlock online whitelist sync server")
    parser.add_argument("--host", default="0.0.0.0", help="listen host")
    parser.add_argument("--port", type=int, default=8787, help="listen port")
    parser.add_argument("--retention-ms", type=int, default=120_000, help="server-side stale host retention")
    args = parser.parse_args()

    registry = OnlineRegistry(retention_ms=args.retention_ms)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(registry))
    print(f"online whitelist server listening on http://{args.host}:{args.port}")
    print("POST /api/online, GET /api/online?maxAgeMs=30000")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nshutting down")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
