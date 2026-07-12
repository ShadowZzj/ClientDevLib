#!/usr/bin/env python3
"""Monitor Linux processes and report Zabbix matches to a WeCom webhook."""

import argparse
import datetime as dt
import json
import logging
import math
import os
import pwd
import shlex
import socket
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Set, Tuple
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlparse
from urllib.request import Request, urlopen


WEBHOOK_ENV = "WECOM_WEBHOOK_URL"
DEFAULT_PATTERN = "zabbix"
DEFAULT_INTERVAL_SECONDS = 60.0
MESSAGE_BODY_BYTES = 1800
MAX_MESSAGE_PARTS = 10
DEFAULT_MAX_PROCESSES_PER_REPORT = 25


class WebhookError(RuntimeError):
    pass


class ProcessAccessDenied(RuntimeError):
    pass


class ProcessInfo:
    __slots__ = (
        "pid",
        "ppid",
        "start_time_ticks",
        "user",
        "name",
        "executable",
        "cwd",
        "command_line",
    )

    def __init__(
        self,
        pid: int,
        ppid: int,
        start_time_ticks: str,
        user: str,
        name: str,
        executable: str,
        cwd: str,
        command_line: str,
    ) -> None:
        self.pid = pid
        self.ppid = ppid
        self.start_time_ticks = start_time_ticks
        self.user = user
        self.name = name
        self.executable = executable
        self.cwd = cwd
        self.command_line = command_line

    @property
    def identity(self) -> Tuple[int, str]:
        return self.pid, self.start_time_ticks


def sanitize_one_line(value: str) -> str:
    escaped = []
    replacements = {"\\": "\\\\", "\r": "\\r", "\n": "\\n", "\t": "\\t"}
    for character in value:
        if character in replacements:
            escaped.append(replacements[character])
        elif character.isprintable():
            escaped.append(character)
        else:
            escaped.append("\\u{:04x}".format(ord(character)))
    return "".join(escaped).encode("utf-8", errors="replace").decode("utf-8")


def truncate_utf8(value: str, max_bytes: int) -> str:
    encoded = value.encode("utf-8")
    if len(encoded) <= max_bytes:
        return value

    suffix = "..."
    room = max_bytes - len(suffix.encode("utf-8"))
    if room <= 0:
        return suffix[:max_bytes]
    return encoded[:room].decode("utf-8", errors="ignore") + suffix


def split_utf8(value: str, max_bytes: int) -> List[str]:
    parts: List[str] = []
    remaining = value
    while remaining:
        encoded = remaining.encode("utf-8")
        if len(encoded) <= max_bytes:
            parts.append(remaining)
            break

        candidate = encoded[:max_bytes].decode("utf-8", errors="ignore")
        newline = candidate.rfind("\n")
        if newline >= len(candidate) // 2:
            candidate = candidate[: newline + 1]
        if not candidate:
            raise ValueError("max_bytes is too small for UTF-8 text")

        parts.append(candidate)
        remaining = remaining[len(candidate) :]
    return parts or [""]


def parse_proc_stat(value: str) -> Tuple[str, int, str]:
    left_paren = value.find("(")
    right_paren = value.rfind(")")
    if left_paren < 0 or right_paren <= left_paren:
        raise ValueError("invalid /proc stat format")

    name = value[left_paren + 1 : right_paren]
    fields = value[right_paren + 1 :].split()
    if len(fields) < 20:
        raise ValueError("incomplete /proc stat data")

    ppid = int(fields[1])
    start_time_ticks = fields[19]
    return name, ppid, start_time_ticks


def read_limited(path: Path, limit: int = 65536) -> bytes:
    with path.open("rb") as stream:
        return stream.read(limit)


def read_link(path: Path) -> Tuple[str, str]:
    try:
        value = os.readlink(str(path))
        return value, value
    except PermissionError:
        return "<permission denied>", ""
    except FileNotFoundError:
        raise
    except OSError as exc:
        return "<unavailable: {}>".format(exc.strerror or exc.__class__.__name__), ""


def read_command_line(path: Path, fallback_name: str) -> Tuple[List[str], str]:
    try:
        raw = read_limited(path)
    except PermissionError:
        return [], "<permission denied>"
    except FileNotFoundError:
        raise
    except OSError as exc:
        return [], "<unavailable: {}>".format(exc.strerror or exc.__class__.__name__)

    arguments = [
        item.decode("utf-8", errors="replace")
        for item in raw.rstrip(b"\0").split(b"\0")
        if item
    ]
    if not arguments:
        return [], "[{}]".format(fallback_name)
    return arguments, " ".join(shlex.quote(argument) for argument in arguments)


def read_process(proc_dir: Path, pattern: str) -> Optional[ProcessInfo]:
    try:
        stat_value = read_limited(proc_dir / "stat", 4096).decode("utf-8", errors="replace")
        stat_name, ppid, start_time_ticks = parse_proc_stat(stat_value)

        try:
            name = read_limited(proc_dir / "comm", 4096).decode(
                "utf-8", errors="replace"
            ).rstrip("\n")
        except PermissionError:
            name = stat_name

        executable, executable_for_match = read_link(proc_dir / "exe")
        cwd, _ = read_link(proc_dir / "cwd")
        arguments, command_line = read_command_line(proc_dir / "cmdline", name)
        uid = proc_dir.stat().st_uid
        verified_stat = read_limited(proc_dir / "stat", 4096).decode(
            "utf-8", errors="replace"
        )
        _, _, verified_start_time = parse_proc_stat(verified_stat)
    except (FileNotFoundError, ProcessLookupError):
        return None
    except PermissionError as exc:
        raise ProcessAccessDenied(proc_dir.name) from exc
    except (OSError, ValueError) as exc:
        logging.debug("cannot inspect PID %s: %s", proc_dir.name, exc)
        return None

    if verified_start_time != start_time_ticks:
        return None

    candidates = [name]
    if executable_for_match:
        candidates.append(os.path.basename(executable_for_match))
    if arguments:
        candidates.append(os.path.basename(arguments[0]))

    folded_pattern = pattern.casefold()
    if not any(folded_pattern in candidate.casefold() for candidate in candidates):
        return None

    try:
        user = pwd.getpwuid(uid).pw_name
    except KeyError:
        user = str(uid)

    return ProcessInfo(
        pid=int(proc_dir.name),
        ppid=ppid,
        start_time_ticks=start_time_ticks,
        user=user,
        name=sanitize_one_line(name),
        executable=sanitize_one_line(executable),
        cwd=sanitize_one_line(cwd),
        command_line=sanitize_one_line(command_line),
    )


def scan_processes(pattern: str, proc_root: Path = Path("/proc")) -> List[ProcessInfo]:
    matches: List[ProcessInfo] = []
    denied_count = 0
    own_pid = str(os.getpid())
    try:
        entries = list(os.scandir(str(proc_root)))
    except OSError as exc:
        raise RuntimeError("cannot scan {}: {}".format(proc_root, exc)) from exc

    for entry in entries:
        if not entry.name.isdigit() or entry.name == own_pid:
            continue
        try:
            process = read_process(proc_root / entry.name, pattern)
        except ProcessAccessDenied:
            denied_count += 1
            continue
        if process is not None:
            matches.append(process)
    if denied_count:
        logging.warning(
            "could not inspect %d process(es); check /proc permissions", denied_count
        )
    return sorted(matches, key=lambda item: item.pid)


def format_process(process: ProcessInfo) -> str:
    return "\n".join(
        [
            "PID: {}  PPID: {}  USER: {}".format(process.pid, process.ppid, process.user),
            "NAME: {}".format(truncate_utf8(process.name, 256)),
            "EXE: {}".format(truncate_utf8(process.executable, 768)),
            "CWD: {}".format(truncate_utf8(process.cwd, 768)),
            "CMD: {}".format(truncate_utf8(process.command_line, 1200)),
        ]
    )


def build_report(
    processes: Sequence[ProcessInfo],
    pattern: str,
    total_matches: int,
    omitted: int = 0,
) -> str:
    now = dt.datetime.now().astimezone().isoformat(timespec="seconds")
    lines = [
        "[Process monitor alert]",
        "HOST: {}".format(socket.gethostname()),
        "TIME: {}".format(now),
        "PATTERN: {}".format(truncate_utf8(sanitize_one_line(pattern), 256)),
        "REPORTED: {}  TOTAL MATCHING: {}".format(len(processes), total_matches),
    ]
    for process in processes:
        lines.extend(["", format_process(process)])

    if omitted > 0:
        lines.extend(
            [
                "",
                "{} additional matching process(es) omitted from this report.".format(
                    omitted
                ),
            ]
        )
    return "\n".join(lines)


def fit_report(
    processes: Sequence[ProcessInfo],
    pattern: str,
    total_matches: int,
    max_processes: int,
) -> Tuple[List[ProcessInfo], str]:
    selected = list(processes[:max_processes])
    while selected:
        report = build_report(
            selected,
            pattern,
            total_matches,
            omitted=len(processes) - len(selected),
        )
        if len(split_utf8(report, MESSAGE_BODY_BYTES)) <= MAX_MESSAGE_PARTS:
            return selected, report
        selected.pop()

    raise ValueError("a single process report exceeds the message limit")


def prepare_messages(report: str) -> List[str]:
    parts = split_utf8(report, MESSAGE_BODY_BYTES)
    original_count = len(parts)
    if original_count > MAX_MESSAGE_PARTS:
        parts = parts[:MAX_MESSAGE_PARTS]
        notice = "\n[Report truncated: {} additional part(s) omitted]".format(
            original_count - MAX_MESSAGE_PARTS
        )
        available = MESSAGE_BODY_BYTES - len(notice.encode("utf-8"))
        parts[-1] = truncate_utf8(parts[-1], available) + notice

    count = len(parts)
    if count == 1:
        return parts
    return [
        "[Part {}/{}]\n{}".format(index, count, part)
        for index, part in enumerate(parts, start=1)
    ]


def validate_webhook(webhook: str) -> None:
    parsed = urlparse(webhook)
    key = parse_qs(parsed.query).get("key", [""])[0]
    if (
        parsed.scheme != "https"
        or parsed.hostname != "qyapi.weixin.qq.com"
        or parsed.path != "/cgi-bin/webhook/send"
        or not key
    ):
        raise ValueError("invalid WeCom webhook URL")


def send_message(webhook: str, content: str, timeout: float = 10.0) -> None:
    payload = json.dumps(
        {"msgtype": "text", "text": {"content": content}},
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    request = Request(
        webhook,
        data=payload,
        headers={"Content-Type": "application/json; charset=utf-8"},
        method="POST",
    )

    try:
        with urlopen(request, timeout=timeout) as response:
            response_body = response.read(65536).decode("utf-8", errors="replace")
    except HTTPError as exc:
        body = exc.read(2048).decode("utf-8", errors="replace")
        raise WebhookError("HTTP {}: {}".format(exc.code, sanitize_one_line(body))) from exc
    except URLError as exc:
        raise WebhookError("network error: {}".format(exc.reason)) from exc
    except (TimeoutError, OSError) as exc:
        raise WebhookError("network error: {}".format(exc)) from exc

    try:
        result = json.loads(response_body)
    except json.JSONDecodeError as exc:
        raise WebhookError("non-JSON webhook response") from exc
    if not isinstance(result, dict):
        raise WebhookError("invalid webhook response")
    if result.get("errcode") != 0:
        raise WebhookError(
            "WeCom error {}: {}".format(result.get("errcode"), result.get("errmsg"))
        )


def positive_number(value: str) -> float:
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def positive_integer(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Report Linux processes whose name contains a pattern to WeCom."
    )
    parser.add_argument("--pattern", default=DEFAULT_PATTERN)
    parser.add_argument(
        "--interval",
        type=positive_number,
        default=DEFAULT_INTERVAL_SECONDS,
        help="scan interval in seconds (default: 60)",
    )
    parser.add_argument(
        "--only-new",
        action="store_true",
        help="notify once per process during this script run instead of on every scan",
    )
    parser.add_argument(
        "--max-processes",
        type=positive_integer,
        default=DEFAULT_MAX_PROCESSES_PER_REPORT,
        help="maximum process details per report (default: 25)",
    )
    parser.add_argument("--once", action="store_true", help="scan once and exit")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print messages without calling the webhook",
    )
    args = parser.parse_args(argv)
    if not args.pattern:
        parser.error("--pattern cannot be empty")
    if not args.dry_run and not os.environ.get(WEBHOOK_ENV):
        parser.error("{} is not set".format(WEBHOOK_ENV))
    return args


def run(args: argparse.Namespace) -> int:
    webhook = os.environ.get(WEBHOOK_ENV, "")
    if webhook:
        try:
            validate_webhook(webhook)
        except ValueError as exc:
            logging.error("%s", exc)
            return 2

    notified: Set[Tuple[int, str]] = set()
    missing_cycles: Dict[Tuple[int, str], int] = {}
    report_offset = 0
    while True:
        scan_started = time.monotonic()
        try:
            matches = scan_processes(args.pattern)
        except RuntimeError as exc:
            logging.error("scan failed: %s", exc)
            if args.once:
                return 1
            elapsed = time.monotonic() - scan_started
            try:
                time.sleep(max(0.0, args.interval - elapsed))
            except KeyboardInterrupt:
                logging.info("stopped")
                return 0
            continue

        current = {process.identity for process in matches}
        if args.only_new:
            for identity in list(notified):
                if identity in current:
                    missing_cycles.pop(identity, None)
                    continue
                missing_cycles[identity] = missing_cycles.get(identity, 0) + 1
                if missing_cycles[identity] >= 3:
                    notified.remove(identity)
                    missing_cycles.pop(identity, None)
            targets = [
                process for process in matches if process.identity not in notified
            ]
        elif matches:
            report_offset %= len(matches)
            targets = matches[report_offset:] + matches[:report_offset]
        else:
            report_offset = 0
            targets = []

        delivery_failed = False
        if targets:
            selected, report = fit_report(
                targets, args.pattern, len(matches), args.max_processes
            )
            messages = prepare_messages(report)
            try:
                if args.dry_run:
                    for message in messages:
                        print(message)
                        print("-" * 72)
                else:
                    for message in messages:
                        send_message(webhook, message)
                if args.only_new:
                    notified.update(process.identity for process in selected)
                else:
                    report_offset = (report_offset + len(selected)) % len(matches)
                logging.info(
                    "reported %d process(es) in %d message(s)",
                    len(selected),
                    len(messages),
                )
            except WebhookError as exc:
                delivery_failed = True
                logging.error("webhook delivery failed: %s", exc)
        else:
            logging.info(
                "scan complete: %d matching process(es), nothing to report", len(matches)
            )

        if args.once:
            return 1 if delivery_failed else 0
        elapsed = time.monotonic() - scan_started
        try:
            time.sleep(max(0.0, args.interval - elapsed))
        except KeyboardInterrupt:
            logging.info("stopped")
            return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    if not sys.platform.startswith("linux") or not Path("/proc").is_dir():
        logging.error("this script requires Linux procfs mounted at /proc")
        return 2
    if os.geteuid() != 0:
        logging.warning(
            "running without root privileges; some process details may be unavailable"
        )
    return run(parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
