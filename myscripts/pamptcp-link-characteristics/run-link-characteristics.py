#!/usr/bin/env python3
"""Generate and run single-link Wi-Fi/NR characteristic sweeps.

Each generated traffic profile contains one template.  The RateDual app sends
one timestamped TCP application unit per second; the unit size is controlled by
the profile rate: bytes_per_second * 8 bps.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
from typing import Iterable


SCRIPT = "myscripts/pamptcp-link-characteristics/link-characteristics.cc"
TARGET = "bin/link-characteristics"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def parse_csv_uints(text: str) -> list[int]:
    values: list[int] = []
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        value = int(part)
        if value <= 0:
            raise ValueError(f"expected positive integer, got {value}")
        values.append(value)
    if not values:
        raise ValueError("empty integer list")
    return values


def parse_links(text: str) -> list[str]:
    links = [part.strip() for part in text.split(",") if part.strip()]
    valid = {"wifi-only", "nr-only"}
    unknown = [link for link in links if link not in valid]
    if unknown:
        raise ValueError(f"unknown links: {','.join(unknown)}")
    if not links:
        raise ValueError("empty link list")
    return links


def data_rate_for_bytes(bytes_per_second: int) -> str:
    return f"{bytes_per_second * 8}bps"


def profile_name(num_clients: int, bytes_per_second: int) -> str:
    return f"n{num_clients:03d}-b{bytes_per_second}"


def write_profile(profile_dir: Path, num_clients: int, bytes_per_second: int) -> None:
    profile_dir.mkdir(parents=True, exist_ok=True)
    rate = data_rate_for_bytes(bytes_per_second)
    template_id = f"b{bytes_per_second}"
    (profile_dir / "templates.csv").write_text(
        "\n".join(
            [
                "template_id,priority,appSteadyRate,appBurstRate,appTrafficModel,appSteadyTime,appBurstTime,appInitialSendDelay",
                f"{template_id},3,{rate},{rate},duration,ns3::ConstantRandomVariable[Constant=1.0],ns3::ConstantRandomVariable[Constant=0.0],0",
                "",
            ]
        ),
        encoding="utf-8",
    )
    (profile_dir / "groups.csv").write_text(
        "\n".join(["template_id,count", f"{template_id},{num_clients}", ""]),
        encoding="utf-8",
    )


def build_once(root: Path, mode: str, jobs: int) -> None:
    out_dir = "build-opt-dce-rel" if mode == "opt" else "build"
    c4che = root / out_dir / "c4che"
    if not c4che.exists():
        raise SystemExit(f"missing configured {out_dir}; run ./build-all.sh --mode {mode} first")
    cmd = ["python3", "./waf", "build", f"--targets={TARGET}", "-j", str(jobs), "-o", out_dir]
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=root, check=True)


def env_for_mode(root: Path, mode: str, dce_files_dir: Path) -> dict[str, str]:
    env = os.environ.copy()
    if mode == "opt":
        ld_library_path = ":".join(
            [
                str(root / "build-opt-dce-rel/lib"),
                "/root/bake/build-opt-rel/lib",
                "/root/bake/build-opt-dce-rel/lib",
            ]
        )
        dce_path = ":".join(
            [
                str(root / "build-opt-dce-rel/bin"),
                str(root / "build-opt-dce-rel/bin_dce"),
                "/root/bake/build-opt-dce-rel/bin",
                "/root/bake/build-opt-dce-rel/bin_dce",
                "/root/bake/build/sbin",
                "/root/bake/build/bin_dce",
            ]
        )
    else:
        ld_library_path = ":".join(
            [
                str(root / "build/lib"),
                "/root/bake/build/lib",
            ]
        )
        dce_path = ":".join(
            [
                str(root / "build/bin"),
                str(root / "build/bin_dce"),
                "/root/bake/build/bin",
                "/root/bake/build/bin_dce",
                "/root/bake/build/sbin",
            ]
        )

    if env.get("LD_LIBRARY_PATH"):
        ld_library_path = f"{ld_library_path}:{env['LD_LIBRARY_PATH']}"
    if env.get("DCE_PATH"):
        dce_path = f"{dce_path}:{env['DCE_PATH']}"

    env["LD_LIBRARY_PATH"] = ld_library_path
    env["DCE_PATH"] = dce_path
    env["PAMPTCP_BASE_DIR"] = str(root)
    env["DCE_FILES_DIR"] = str(dce_files_dir)
    return env


def binary_for_mode(root: Path, mode: str) -> Path:
    out_dir = "build-opt-dce-rel" if mode == "opt" else "build"
    return root / out_dir / "myscripts/pamptcp-link-characteristics/bin/link-characteristics"


def expected_json(profile_dir: Path, link: str, scheduler: str, stream: int, sim_time: float) -> Path:
    duration = f"{sim_time:.6f}".rstrip("0").rstrip(".")
    duration = duration.replace(".", "p") or "0"
    path_link = f"-{link}" if link != "dual" else ""
    return profile_dir / f"{scheduler}{path_link}-stream{stream}-{duration}s.json"


def run_one(
    root: Path,
    mode: str,
    link: str,
    profile_dir: Path,
    args: argparse.Namespace,
) -> tuple[str, Path, int, float]:
    report = expected_json(profile_dir, link, args.scheduler, args.client_start_jitter_stream, args.sim_time)
    if args.skip_existing and report.exists():
        return (link, profile_dir, 0, 0.0)

    dce_tmp = Path(tempfile.mkdtemp(prefix="ns3-dce-files."))
    log_path = profile_dir / f"run-{link}.log"
    program_args = [
        f"--trafficProfileDir={profile_dir}",
        f"--numAps={args.num_aps}",
        f"--pathMode={link}",
        f"--simTime={args.sim_time}",
        f"--statsStart={args.stats_start}",
        f"--statsStop={args.stats_stop}",
        f"--clientStartJitter={args.client_start_jitter}",
        f"--clientStartJitterStream={args.client_start_jitter_stream}",
        f"--mptcpScheduler={args.scheduler}",
    ]
    cmd = [str(binary_for_mode(root, mode)), *program_args]
    start = time.monotonic()
    rc = 1
    try:
        with log_path.open("w", encoding="utf-8") as log:
            log.write("+ " + " ".join(cmd) + "\n")
            log.write(f"DCE_FILES_DIR={dce_tmp}\n")
            log.flush()
            completed = subprocess.run(
                cmd,
                cwd=root,
                env=env_for_mode(root, mode, dce_tmp),
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=args.timeout if args.timeout > 0 else None,
                check=False,
            )
            rc = completed.returncode
    except subprocess.TimeoutExpired:
        rc = 124
        with log_path.open("a", encoding="utf-8") as log:
            log.write(f"\nTIMEOUT after {args.timeout}s\n")
    finally:
        if args.keep_dce_files:
            with log_path.open("a", encoding="utf-8") as log:
                log.write(f"\nkept DCE files at {dce_tmp}\n")
        else:
            shutil.rmtree(dce_tmp, ignore_errors=True)

    elapsed = time.monotonic() - start
    return (link, profile_dir, rc, elapsed)


def iter_jobs(args: argparse.Namespace) -> Iterable[tuple[str, int, int]]:
    links = parse_links(args.links)
    clients = parse_csv_uints(args.clients)
    wifi_bytes = parse_csv_uints(args.wifi_bytes)
    nr_bytes = parse_csv_uints(args.nr_bytes)
    for link in links:
        sizes = wifi_bytes if link == "wifi-only" else nr_bytes
        for num_clients in clients:
            for bytes_per_second in sizes:
                yield link, num_clients, bytes_per_second


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=["debug", "opt"], default="opt")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--parallel", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--links", default="wifi-only,nr-only")
    parser.add_argument("--clients", default="1,4,8,15,30")
    parser.add_argument("--wifi-bytes", default="64,1024,16384,65536,262144,1048576,2097152,4194304")
    parser.add_argument("--nr-bytes", default="64,1024,16384,65536,262144,524288,1048576,2097152")
    parser.add_argument("--profile-root", default="myscripts/pamptcp-link-characteristics/flow-profiles/final-grid")
    parser.add_argument("--generate-only", action="store_true")
    parser.add_argument("--skip-existing", action="store_true")
    parser.add_argument("--keep-dce-files", action="store_true")
    parser.add_argument("--fail-fast", action="store_true")
    parser.add_argument("--sim-time", type=float, default=10.0)
    parser.add_argument("--stats-start", type=float, default=4.0)
    parser.add_argument("--stats-stop", type=float, default=10.0)
    parser.add_argument("--client-start-jitter", type=float, default=1.0)
    parser.add_argument("--client-start-jitter-stream", type=int, default=1)
    parser.add_argument("--num-aps", type=int, default=1)
    parser.add_argument("--scheduler", default="default")
    args = parser.parse_args()

    root = repo_root()
    profile_root = (root / args.profile_root).resolve()

    jobs = list(iter_jobs(args))
    unique_profiles: dict[tuple[int, int], Path] = {}
    for _link, num_clients, bytes_per_second in jobs:
        profile_dir = profile_root / profile_name(num_clients, bytes_per_second)
        unique_profiles[(num_clients, bytes_per_second)] = profile_dir
        write_profile(profile_dir, num_clients, bytes_per_second)

    print(f"profiles={len(unique_profiles)} runs={len(jobs)} root={profile_root}")
    if args.generate_only:
        return 0

    build_once(root, args.mode, args.jobs)

    failures: list[tuple[str, Path, int]] = []
    max_workers = max(1, args.parallel)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        futures = []
        for link, num_clients, bytes_per_second in jobs:
            profile_dir = profile_root / profile_name(num_clients, bytes_per_second)
            futures.append(executor.submit(run_one, root, args.mode, link, profile_dir, args))

        for future in concurrent.futures.as_completed(futures):
            link, profile_dir, rc, elapsed = future.result()
            rel = profile_dir.relative_to(root)
            status = "ok" if rc == 0 else f"failed rc={rc}"
            print(f"[{status}] {link} {rel} wall={elapsed:.1f}s", flush=True)
            if rc != 0:
                failures.append((link, profile_dir, rc))
                if args.fail_fast:
                    break

    if failures:
        print("failed runs:", file=sys.stderr)
        for link, profile_dir, rc in failures:
            print(f"  {link} {profile_dir} rc={rc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
