#!/usr/bin/env python3
"""Summarize link-characteristic JSON reports into CSV and Markdown tables."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import re
from typing import Any


PROFILE_RE = re.compile(r"^n(?P<clients>\d+)-b(?P<bytes>\d+)$")
REPORT_RE = re.compile(r"^(?P<scheduler>.+)-(?P<link>wifi-only|nr-only)-stream\d+-.+s\.json$")
EXPECTED_WARNINGS = (
    "Single-path mode disables MPTCP subflow setup",
)


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def find_overall(report: dict[str, Any]) -> dict[str, Any]:
    for entry in report.get("priority_metrics", {}).get("entries", []):
        if entry.get("scope") == "overall":
            return entry
    return {}


def load_row(path: Path) -> dict[str, Any] | None:
    profile_match = PROFILE_RE.match(path.parent.name)
    report_match = REPORT_RE.match(path.name)
    if not profile_match or not report_match:
        return None

    with path.open("r", encoding="utf-8") as handle:
        report = json.load(handle)

    overall = find_overall(report)
    delay = overall.get("delay_ms", {})
    jitter = overall.get("jitter_ms", {})
    packet_sink = report.get("packet_sink", {})
    client_connect = report.get("client_connect", {})
    warnings = [
        warning
        for warning in report.get("warnings", [])
        if not any(expected in warning for expected in EXPECTED_WARNINGS)
    ]
    clients = int(profile_match.group("clients"))
    bytes_per_burst = int(profile_match.group("bytes"))
    offered_mbps = clients * bytes_per_burst * 8.0 / 1e6

    return {
        "link": report_match.group("link"),
        "clients": clients,
        "bytes_per_burst": bytes_per_burst,
        "offered_mbps": offered_mbps,
        "rx_mbps": float(overall.get("throughput_mbps", 0.0)),
        "sink_mbps": float(packet_sink.get("throughput_mbps", 0.0)),
        "delay_mean_ms": float(delay.get("mean", 0.0)),
        "delay_p50_ms": float(delay.get("p50", 0.0)),
        "delay_p95_ms": float(delay.get("p95", 0.0)),
        "delay_p99_ms": float(delay.get("p99", 0.0)),
        "delay_max_ms": float(delay.get("max", 0.0)),
        "jitter_mean_ms": float(jitter.get("mean", 0.0)),
        "rx_packets": int(overall.get("rx_packets", 0)),
        "delay_rx_packets": int(overall.get("delay_rx_packets", 0)),
        "active_flows": int(overall.get("active_flows", 0)),
        "connected": int(client_connect.get("connected", 0)),
        "total": int(client_connect.get("total", 0)),
        "connect_failures": int(client_connect.get("total_connect_failures", 0)),
        "warnings": " | ".join(warnings),
        "json": str(path),
    }


def format_float(value: float, digits: int = 3) -> str:
    text = f"{value:.{digits}f}"
    return text.rstrip("0").rstrip(".") if "." in text else text


def write_csv(rows: list[dict[str, Any]], path: Path) -> None:
    fieldnames = [
        "link",
        "clients",
        "bytes_per_burst",
        "offered_mbps",
        "rx_mbps",
        "sink_mbps",
        "delay_mean_ms",
        "delay_p50_ms",
        "delay_p95_ms",
        "delay_p99_ms",
        "delay_max_ms",
        "jitter_mean_ms",
        "rx_packets",
        "delay_rx_packets",
        "active_flows",
        "connected",
        "total",
        "connect_failures",
        "warnings",
        "json",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def write_markdown(rows: list[dict[str, Any]], path: Path, root: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Link Characteristics Summary",
        "",
        "Delay is `priority_metrics.entries[scope=overall].delay_ms`, i.e. application-layer completion time for each timestamped TCP burst.",
        "",
    ]
    for link in ["wifi-only", "nr-only"]:
        link_rows = [row for row in rows if row["link"] == link]
        if not link_rows:
            continue
        lines.extend(
            [
                f"## {link}",
                "",
                "| clients | bytes/burst | offered Mbps | rx Mbps | delay mean ms | p50 | p95 | p99 | max | connected | notes |",
                "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
            ]
        )
        for row in link_rows:
            notes: list[str] = []
            if row["connected"] != row["total"]:
                notes.append(f"connected {row['connected']}/{row['total']}")
            if row["connect_failures"]:
                notes.append(f"connect failures {row['connect_failures']}")
            if row["warnings"]:
                notes.append("warnings")
            lines.append(
                "| "
                + " | ".join(
                    [
                        str(row["clients"]),
                        str(row["bytes_per_burst"]),
                        format_float(row["offered_mbps"]),
                        format_float(row["rx_mbps"]),
                        format_float(row["delay_mean_ms"]),
                        format_float(row["delay_p50_ms"]),
                        format_float(row["delay_p95_ms"]),
                        format_float(row["delay_p99_ms"]),
                        format_float(row["delay_max_ms"]),
                        f"{row['connected']}/{row['total']}",
                        "; ".join(notes),
                    ]
                )
                + " |"
            )
        lines.append("")
    lines.extend(
        [
            "## Files",
            "",
            f"- CSV: `{path.with_suffix('.csv').relative_to(root)}`",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile_root", nargs="?", default="myscripts/pamptcp-link-characteristics/flow-profiles/final-grid")
    parser.add_argument("--csv", default="")
    parser.add_argument("--md", default="")
    args = parser.parse_args()

    root = repo_root()
    profile_root = (root / args.profile_root).resolve()
    rows: list[dict[str, Any]] = []
    for json_path in sorted(profile_root.glob("n*-b*/*.json")):
        row = load_row(json_path)
        if row is not None:
            rows.append(row)

    rows.sort(key=lambda row: (row["link"], row["clients"], row["bytes_per_burst"]))

    csv_path = Path(args.csv) if args.csv else profile_root / "summary.csv"
    md_path = Path(args.md) if args.md else profile_root / "summary.md"
    if not csv_path.is_absolute():
        csv_path = root / csv_path
    if not md_path.is_absolute():
        md_path = root / md_path

    write_csv(rows, csv_path)
    write_markdown(rows, md_path, root)
    print(f"rows={len(rows)} csv={csv_path} md={md_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
