bench_plot.py
----------------

Utility to run the Catch2 benchmark suite (the project's test binary) and
produce SVG plots of the reported benchmark means.

Features
- Run the test binary with Catch2's XML reporter and extract <BenchmarkResults/>
- Produce one SVG per benchmark (e.g. single-threaded, multi-threaded)
- Auto-detect large outliers and render a "broken" stacked-axis plot for readability
- CLI flags to force `linear` or `broken` modes
- Optional per-benchmark CSV output

Usage
1. Build the test binary (see project README). Example build target is `nova_sync_tests`.
2. Run the tool:

```sh
python3 tools/bench_plot.py --binary /path/to/nova_sync_tests --bench "[!benchmark]" --out /tmp/bench.svg --runner-args "--benchmark-samples 5"
```

Options
- --binary PATH        Path to the test binary (required)
- --bench FILTER       Optional Catch2 filter (positional) to select benchmarks (e.g. "[!benchmark]")
- --runner-args ARGS   Extra args passed to the test runner (quoted)
- --mode MODE          Plotting mode: auto|broken|linear (default: auto)
- --out PATH           Base output path for SVGs (default: bench.svg)
- --out-csv PATH       Optional base path for per-benchmark CSVs; generates <base>_<bench>.csv

Environment Variables
- BENCH_PLOT_LINTHRESH   Override linear threshold (ms) used when detecting outliers
- BENCH_PLOT_MULTIPLIER  Ratio threshold for gap detection (default 10)
- BENCH_PLOT_ABSGAP      Absolute gap threshold (ms); default max(1000, lin_thresh*10)

Notes
- The script requires matplotlib. Install with `pip install matplotlib`.
- The CSV output can be used to create alternative plots or for archival.
