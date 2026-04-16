#!/usr/bin/env python3
"""
Pull crawl logs from all 7 GCP VMs and generate comparison graphs.
Usage: python3 plot_crawl.py [--key ~/.ssh/gcp_key] [--user ashmits] [--out ./graphs]
"""

import subprocess
import re
import sys
import os
import argparse
from datetime import datetime
from collections import defaultdict

VMS = [
    "34.130.43.20",
    "34.130.82.156",
    "34.124.113.238",
    "34.130.64.46",
    "34.130.43.3",
    "34.130.137.49",
    "34.130.64.163",
]

HEARTBEAT_RE = re.compile(
    r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\].*"
    r"documents processed = (\d+).*"
    r"frontier = (\d+)"
    r"(?:.*links discovered = (\d+))?"
)


def fetch_log(ip, key, user):
    result = subprocess.run(
        ["ssh", "-o", "ConnectTimeout=10", "-o", "StrictHostKeyChecking=no",
         "-i", key, f"{user}@{ip}", "cat ~/Search-Engines/crawl.log"],
        capture_output=True, text=True
    )
    return result.stdout


def parse_log(text):
    rows = []
    for line in text.splitlines():
        m = HEARTBEAT_RE.search(line)
        if m:
            ts = datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S")
            docs = int(m.group(2))
            frontier = int(m.group(3))
            links = int(m.group(4)) if m.group(4) else None
            rows.append((ts, docs, frontier, links))
    return rows


def aggregate(all_rows):
    by_time = defaultdict(lambda: {"docs": 0, "frontier": 0, "links": 0, "vms": 0})
    for rows in all_rows:
        for ts, docs, frontier, links in rows:
            bucket = ts.replace(second=0)
            by_time[bucket]["docs"] += docs
            by_time[bucket]["frontier"] += frontier
            by_time[bucket]["links"] += links or 0
            by_time[bucket]["vms"] += 1

    times = sorted(by_time)
    docs = [by_time[t]["docs"] for t in times]
    frontiers = [by_time[t]["frontier"] for t in times]
    links = [by_time[t]["links"] for t in times]
    return times, docs, frontiers, links


def compute_speed(times, docs):
    speeds = [0]
    for i in range(1, len(times)):
        dt_hrs = (times[i] - times[i - 1]).total_seconds() / 3600
        dd = docs[i] - docs[i - 1]
        speeds.append(dd / dt_hrs if dt_hrs > 0 else 0)
    return speeds


def plot(times, docs, speeds, frontiers, links, out_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates

    os.makedirs(out_dir, exist_ok=True)
    fmt = mdates.DateFormatter("%m-%d %H")

    def save(fig, name):
        path = os.path.join(out_dir, name)
        fig.savefig(path, dpi=150, bbox_inches="tight")
        print(f"  Saved {path}")
        plt.close(fig)

    # 1. Documents processed
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(times, docs, color="steelblue")
    ax.set_title("Number of Documents Processed Over Time")
    ax.set_xlabel("Time")
    ax.set_ylabel("Number of Documents Processed")
    ax.xaxis.set_major_formatter(fmt)
    ax.grid(True, alpha=0.4)
    fig.autofmt_xdate()
    save(fig, "docs_processed.png")

    # 2. Crawling speed
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(times, speeds, color="firebrick")
    ax.set_title("Crawling Speed Over Time (Documents per Hour)")
    ax.set_xlabel("Time")
    ax.set_ylabel("Crawling Speed (Documents per Hour)")
    ax.xaxis.set_major_formatter(fmt)
    ax.grid(True, alpha=0.4)
    fig.autofmt_xdate()
    save(fig, "crawl_speed.png")

    # 3. Frontier size
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(times, frontiers, color="seagreen")
    ax.set_title("Number of Frontier Items Over Time")
    ax.set_xlabel("Time")
    ax.set_ylabel("Number of Frontier Items")
    ax.xaxis.set_major_formatter(fmt)
    ax.grid(True, alpha=0.4)
    fig.autofmt_xdate()
    save(fig, "frontier_size.png")

    # 4. Links discovered (cumulative)
    if any(l > 0 for l in links):
        fig, ax = plt.subplots(figsize=(12, 5))
        ax.plot(times, links, color="darkorange")
        ax.set_title("Cumulative Links Discovered Over Time")
        ax.set_xlabel("Time")
        ax.set_ylabel("Links Discovered (Unique, Post-Bloom)")
        ax.xaxis.set_major_formatter(fmt)
        ax.grid(True, alpha=0.4)
        fig.autofmt_xdate()
        save(fig, "links_discovered.png")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--key", default=os.path.expanduser("~/.ssh/gcp_key"))
    parser.add_argument("--user", default="ashmits")
    parser.add_argument("--out", default="./graphs")
    args = parser.parse_args()

    print("Fetching logs...")
    all_rows = []
    for ip in VMS:
        print(f"  {ip}...", end=" ", flush=True)
        text = fetch_log(ip, args.key, args.user)
        rows = parse_log(text)
        print(f"{len(rows)} heartbeats")
        all_rows.append(rows)

    if all(len(r) == 0 for r in all_rows):
        print("No heartbeat lines found yet — wait 5 minutes for the first one.")
        sys.exit(1)

    times, docs, frontiers, links = aggregate(all_rows)
    speeds = compute_speed(times, docs)

    print(f"\nAggregated {len(times)} time points")
    if docs:
        print(f"  Total docs processed: {docs[-1]:,}")
        print(f"  Current frontier:     {frontiers[-1]:,}")
        print(f"  Peak speed:           {max(speeds):,.0f} docs/hr")

    print("\nGenerating graphs...")
    plot(times, docs, speeds, frontiers, links, args.out)
    print(f"\nDone. Graphs saved to {os.path.abspath(args.out)}/")


if __name__ == "__main__":
    main()
