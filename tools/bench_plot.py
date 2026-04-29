#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 Tim Blechmann

"""
Run the Catch2 benchmarks and plot results as SVG(s).

Usage:
  - Build the test binary (see project README).
  - Run: python3 tools/bench_plot.py \
        --binary /path/to/nova_sync_tests \
        --out bench.svg

The script runs the Catch2 test binary with the XML reporter and extracts
<BenchmarkResults/> elements. For each benchmark it plots the mean time
(milliseconds). By default the script uses an "auto" strategy which creates
either a single linear axis or a broken-axis (stacked linear axes) layout when
there are large outliers. Use --mode to force `linear` or `broken` behavior.
The script can also emit per-benchmark CSV files via --out-csv.
"""

import argparse
import csv
import math
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict, OrderedDict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except Exception as e:
    print("matplotlib is required to generate plots: pip install matplotlib", file=sys.stderr)
    raise


def run_catch_xml(binary, extra_args=None):
    args = [binary, "--reporter", "xml"]
    # allow passing a Catch2 filter (positional) via extra_args
    if extra_args:
        args += extra_args

    proc = subprocess.run(args, capture_output=True, text=True)
    if proc.returncode != 0:
        print(proc.stdout)
        print(proc.stderr, file=sys.stderr)
        raise SystemExit(f"catch2 runner failed: {proc.returncode}")
    return proc.stdout


def extract_benchmarks_from_xml(xml_text):
    """
    Parse Catch2 XML output and group benchmarks by benchmark name.

    Returns a mapping: { bench_name -> OrderedDict(subject -> mean_ms) }
    where 'subject' is the test case subject (e.g. `std::mutex`) and
    mean_ms is the benchmark mean in milliseconds.
    """
    grouped = defaultdict(OrderedDict)
    root = ET.fromstring(xml_text)

    # Iterate over TestCase elements
    for tc in root.findall("TestCase"):
        tc_name = tc.get("name", "")
        # Try to extract the subject after ' - ' (e.g. "mutex benchmarks - std::mutex")
        subject = tc_name.split(" - ", 1)[1] if " - " in tc_name else tc_name

        # For each Section in the TestCase, look for BenchmarkResults
        for sec in tc.findall("Section"):
            for br in sec.findall("BenchmarkResults"):
                bench_name = br.get("name", "")
                mean_elem = br.find("mean")
                if mean_elem is None:
                    continue
                val = mean_elem.get("value")
                if val is None:
                    continue
                try:
                    mean_ns = float(val)
                except Exception:
                    continue

                mean_ms = mean_ns / 1e6
                # Preserve insertion order for subjects
                if subject not in grouped[bench_name]:
                    grouped[bench_name][subject] = mean_ms

    return grouped


def plot_group_benchmarks(grouped, out_prefix):
    """
    Create one SVG per benchmark name (e.g. single-threaded, multi-threaded).

    grouped: mapping bench_name -> OrderedDict(subject -> mean_ms)
    out_prefix: base output filename; bench name will be appended.
    """
    # Prefer a deterministic order: single-threaded first, then multi-threaded
    preferred = ["single-threaded", "multi-threaded"]
    bench_names = []
    for name in preferred:
        if name in grouped:
            bench_names.append(name)
    for name in grouped.keys():
        if name not in bench_names:
            bench_names.append(name)

    for bench_name in bench_names:
        data = grouped.get(bench_name, {})
        if not data:
            continue

        # Sort entries by increasing values (the user requested sorted order)
        pairs = list(data.items())
        pairs_sorted = sorted(pairs, key=lambda p: p[1])
        subjects = [p[0] for p in pairs_sorted]
        means = [p[1] for p in pairs_sorted]

        # Figure sizing: width scales with number of subjects, height fixed
        width = max(6, 0.6 * max(1, len(subjects)))
        height = 4

        # Default plotting behavior: single axes with linear scale but allow handling
        # of large outliers via symlog or broken axis. Default thresholds can be
        # provided via global settings; for now choose a threshold based on data.
        x = list(range(len(subjects)))

        max_val = max(means)
        # Heuristic threshold: 95th percentile-ish / or a fixed threshold if provided
        # We'll set a default threshold at 100 ms if values are mostly small.
        threshold = 100.0

        # If user set an environment variable to override threshold, pick it up
        try:
            env_thresh = os.environ.get("BENCH_PLOT_LINTHRESH")
            if env_thresh:
                threshold = float(env_thresh)
        except Exception:
            pass

        # Multiplier and absolute-gap thresholds can be configured via env
        try:
            multiplier = float(os.environ.get("BENCH_PLOT_MULTIPLIER", "10"))
        except Exception:
            multiplier = 10.0
        try:
            abs_gap = float(os.environ.get("BENCH_PLOT_ABSGAP", str(max(1000.0, threshold * 10.0))))
        except Exception:
            abs_gap = max(1000.0, threshold * 10.0)

        # Allow caller to override plotting mode via global variable set by main()
        mode = getattr(plot_group_benchmarks, "_mode", "auto")

        # Choose plotting strategy: broken axis if there are large outliers, else linear.
        # We detect large gaps between successive sorted values using multiplier or absolute gap.
        use_broken = False
        if mode == "broken":
            use_broken = True
        elif mode == "linear":
            use_broken = False
        else:  # auto
            if len(means) >= 2:
                for i in range(len(means) - 1):
                    a = means[i]
                    b = means[i + 1]
                    if a <= 0:
                        continue
                    ratio = b / a
                    diff = b - a
                    if ratio >= multiplier or diff >= abs_gap:
                        use_broken = True
                        break

        def percentile(data, p):
            # simple linear interpolation percentile, data is list
            if not data:
                return 0.0
            s = sorted(data)
            n = len(s)
            if n == 1:
                return float(s[0])
            idx = (p / 100.0) * (n - 1)
            lo = int(math.floor(idx))
            hi = int(math.ceil(idx))
            if lo == hi:
                return float(s[int(idx)])
            w = idx - lo
            return s[lo] * (1.0 - w) + s[hi] * w

        if use_broken:
            # Identify split indices where gaps exceed multiplier or abs_gap
            split_idxs = []
            for i in range(len(means) - 1):
                a = means[i]
                b = means[i + 1]
                if a <= 0:
                    continue
                if (b / a) >= multiplier or (b - a) >= abs_gap:
                    split_idxs.append(i)

            # Build clusters as slices of the sorted arrays
            clusters = []
            start = 0
            for idx in split_idxs:
                clusters.append(list(range(start, idx + 1)))
                start = idx + 1
            clusters.append(list(range(start, len(means))))

            k = len(clusters)
            # Choose height ratios: smaller axes for outlier groups, larger for base cluster
            bottom_cluster_size = len(clusters[0])
            bottom_weight = max(3, min(10, max(3, bottom_cluster_size // 2)))
            heights = [1] * (k - 1) + [bottom_weight]

            fig, axes = plt.subplots(
                k,
                1,
                sharex=True,
                gridspec_kw={"height_ratios": heights},
                figsize=(max(8, width), max(3, 2 + k * 1.5)),
                dpi=100,
            )

            # Ensure axes is a list
            if k == 1:
                axes = [axes]

            # Plot each cluster on its own axis; axes[0] is top, clusters[-1] is bottom cluster
            width_bar = 0.6
            for c_idx, cluster in enumerate(clusters):
                ai = k - 1 - c_idx
                ax = axes[ai]

                # For clarity, only draw bars for indices in this cluster
                cluster_x = cluster
                cluster_y = [means[i] for i in cluster]

                # Set axis limits based on cluster's own values
                cmin = min(cluster_y)
                cmax = max(cluster_y)
                if ai == k - 1:
                    # bottom axis -> start at 0
                    y0 = 0.0
                    ax.set_ylim(0, cmax * 1.10 if cmax > 0 else 1.0)
                else:
                    # choose a sensible lower bound for this axis (non-zero)
                    y0 = max(0.0, cmin * 0.9)
                    ax.set_ylim(y0, cmax * 1.10)

                # Draw bars anchored at the axis's lower bound so they don't appear
                # to extend below the visible region when the axis lower bound > 0
                heights = [max(0.0, y - y0) for y in cluster_y]
                ax.bar(cluster_x, heights, width=width_bar, bottom=y0, color="tab:blue")

            # Clean up spines and ticks
            for ai in range(k):
                ax = axes[ai]
                if ai < k - 1:
                    ax.spines["bottom"].set_visible(False)
                if ai > 0:
                    ax.spines["top"].set_visible(False)

            # X tick labels only on bottom axis
            bottom_ax = axes[-1]
            bottom_ax.set_xticks(x)
            bottom_ax.set_xticklabels(subjects, ha="right", rotation=45, fontsize=9)

            # Draw diagonal break indicators between axes
            d = 0.015
            for ai in range(k - 1):
                top_ax = axes[ai]
                bot_ax = axes[ai + 1]
                kwargs = dict(transform=top_ax.transAxes, color="k", clip_on=False)
                top_ax.plot((-d, +d), (-d, +d), **kwargs)
                top_ax.plot((1 - d, 1 + d), (-d, +d), **kwargs)
                kwargs = dict(transform=bot_ax.transAxes, color="k", clip_on=False)
                bot_ax.plot((-d, +d), (1 - d, 1 + d), **kwargs)
                bot_ax.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)

            # Titles/labels
            axes[0].set_title(f"{bench_name}")
            axes[-1].set_ylabel("Mean time (ms)", labelpad=12)

            # Caption describing thresholds used
            # fig.text(0.01, 0.01, caption, ha="left", va="bottom", fontsize=7)

            # Annotate values on each axis using data coordinates
            for c_idx, cluster in enumerate(clusters):
                ai = k - 1 - c_idx
                ax = axes[ai]
                ylim = ax.get_ylim()
                headroom = (ylim[1] - ylim[0]) * 0.02
                for i in cluster:
                    val = means[i]
                    ax.text(i, val + headroom, f"{val:.1f}", ha="center", va="bottom", fontsize=8)

            plt.tight_layout()

        else:
            # No very large outliers -> simple linear plot
            fig, ax = plt.subplots(figsize=(width, height), dpi=100)
            bars = ax.bar(x, means, color="tab:blue", width=0.6)
            ax.set_xticks(x)
            ax.set_xticklabels(subjects, rotation=45, ha="right", fontsize=9)
            ax.set_ylabel("Mean time (ms)")
            ax.set_title(f"{bench_name}")

            top = max_val * 1.10 if max_val > 0 else 1.0
            ax.set_ylim(0, top)
            ax.grid(axis="y", linestyle="--", alpha=0.6)
            for rect, val in zip(bars, means):
                h = rect.get_height()
                ax.text(rect.get_x() + rect.get_width() / 2.0, h + (top * 0.02), f"{val:.3f}", ha="center", va="bottom", fontsize=8)

            plt.tight_layout()

        # Output filename: append sanitized bench name
        safe_name = bench_name.replace(" ", "_").replace("/", "_")
        out_path = f"{os.path.splitext(out_prefix)[0]}_{safe_name}.svg"
        fig.savefig(out_path, format="svg")
        print(f"Wrote benchmark plot to {out_path}")

        # Optionally write CSV with raw values for this bench
        out_csv_base = getattr(plot_group_benchmarks, "_out_csv", None)
        if out_csv_base:
            csv_path = f"{os.path.splitext(out_csv_base)[0]}_{safe_name}.csv"
            try:
                with open(csv_path, "w", newline="") as cf:
                    writer = csv.writer(cf)
                    writer.writerow(["subject", "mean_ms"])
                    for subj, val in pairs_sorted:
                        writer.writerow([subj, f"{val:.6f}"])
                print(f"Wrote CSV data to {csv_path}")
            except Exception as e:
                print(f"Failed to write CSV {csv_path}: {e}", file=sys.stderr)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--binary", required=True, help="Path to nova_sync_tests binary")
    p.add_argument("--out", default="bench.svg", help="Output SVG file")
    p.add_argument("--bench", default=None, help="Optional Catch2 filter (e.g. '[!benchmark]')")
    p.add_argument("--runner-args", default=None, help="Optional additional args passed to the test binary (quoted) e.g. '--benchmark-samples 5 --benchmark-warmup-time 10'")
    p.add_argument("--mode", choices=["auto", "broken", "linear"], default="auto", help="Plotting mode: auto|broken|linear (default: auto)")
    p.add_argument("--out-csv", default=None, help="Optional CSV output base path. Produces per-benchmark CSVs named <base>_<bench>.csv")
    args = p.parse_args()

    binary = args.binary
    if not os.path.isfile(binary) or not os.access(binary, os.X_OK):
        print(f"Binary not found or not executable: {binary}", file=sys.stderr)
        raise SystemExit(2)

    extra = []
    if args.bench:
        extra.append(args.bench)
    if args.runner_args:
        # split runner args shell-style
        import shlex
        extra += shlex.split(args.runner_args)

    xml_text = run_catch_xml(binary, extra_args=extra)
    grouped = extract_benchmarks_from_xml(xml_text)
    if not grouped:
        print("No benchmarks found in test output", file=sys.stderr)
        raise SystemExit(3)
    # Pass mode/out_csv to the plotting function via attributes (simple and avoids changing many signatures)
    setattr(plot_group_benchmarks, "_mode", args.mode)
    setattr(plot_group_benchmarks, "_out_csv", args.out_csv)
    plot_group_benchmarks(grouped, args.out)


if __name__ == "__main__":
    main()
