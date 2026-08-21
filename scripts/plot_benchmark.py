import os
import subprocess
import sys

import matplotlib.pyplot as plt
import pandas as pd


def run_benchmark():
    base_dir = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..")
    )

    build_dir = os.path.join(base_dir, "build")
    logs_dir = os.path.join(base_dir, "logs")

    os.makedirs(build_dir, exist_ok=True)
    os.makedirs(logs_dir, exist_ok=True)

    executable = os.path.join(
        build_dir,
        "benchmark.exe" if os.name == "nt" else "benchmark"
    )

    sources = [
        os.path.join(base_dir, "tests", "benchmark.c"),
        os.path.join(base_dir, "src", "codec.c"),
        os.path.join(base_dir, "src", "distances.c"),
        os.path.join(base_dir, "src", "ranges.c"),
        os.path.join(base_dir, "src", "utils.c"),
    ]

    compile_command = [
        "gcc",
        "-O3",
        "-march=native",
        *sources,
        "-o",
        executable,
        "-lm",
    ]

    print("Compiling benchmark...")

    try:
        subprocess.run(
            compile_command,
            check=True,
            cwd=base_dir,
        )
    except subprocess.CalledProcessError:
        print("Benchmark compilation failed.")
        sys.exit(1)

    print("Running benchmark...")
    print()

    try:
        subprocess.run(
            [executable],
            check=True,
            cwd=base_dir,
        )
    except subprocess.CalledProcessError:
        print("Benchmark execution failed.")
        sys.exit(1)

    return base_dir


def print_summary(df, failure_df):
    print()
    print("=" * 80)
    print("SPATIAL_Z BENCHMARK SUMMARY")
    print("=" * 80)

    for bucket in df["bucket"].unique():
        subset = df[df["bucket"] == bucket]

        runs = len(subset)

        failure_rate = (
            subset["coverage_failure"].mean() * 100.0
        )

        mean_ranges = subset["num_ranges"].mean()
        std_ranges = subset["num_ranges"].std()

        mean_usable = subset["usable_area_pct"].mean()
        std_usable = subset["usable_area_pct"].std()

        mean_dead = 100.0 - mean_usable
        max_dead = 100.0 - subset["usable_area_pct"].min()

        mean_time = subset["exec_time_us"].mean()
        std_time = subset["exec_time_us"].std()
        max_time = subset["exec_time_us"].max()

        print()
        print(f"Radius Bucket: {bucket}")
        print(f"  Runs:                 {runs}")
        print(
            f"  Coverage Failure:     "
            f"{failure_rate:.2f}%"
        )
        print(
            f"  Ranges:               "
            f"{mean_ranges:.2f} +/- {std_ranges:.2f}"
        )
        print(
            f"  Usable Area:          "
            f"{mean_usable:.2f}% +/- {std_usable:.2f}%"
        )
        print(
            f"  Dead Area:            "
            f"{mean_dead:.2f}%"
        )
        print(
            f"  Maximum Dead Area:    "
            f"{max_dead:.2f}%"
        )
        print(
            f"  Execution Time:       "
            f"{mean_time:.2f} +/- {std_time:.2f} us"
        )
        print(
            f"  Maximum Time:         "
            f"{max_time:.2f} us"
        )

        if failure_df is not None and not failure_df.empty:
            failures = failure_df[
                failure_df["bucket"] == bucket
            ]

            if not failures.empty:
                print(
                    f"  Logged Failures:      "
                    f"{len(failures)}"
                )


def create_plots(df):
    buckets = df["bucket"].unique()
    count = len(buckets)

    fig, axes = plt.subplots(
        count,
        3,
        figsize=(16, 5 * count),
        squeeze=False,
        constrained_layout=True,
    )

    for i, bucket in enumerate(buckets):
        subset = df[df["bucket"] == bucket]

        ax_ranges = axes[i, 0]
        ax_dead = axes[i, 1]
        ax_time = axes[i, 2]

        ax_ranges.hist(
            subset["num_ranges"],
            bins=range(
                int(subset["num_ranges"].min()),
                int(subset["num_ranges"].max()) + 2
            ),
            edgecolor="black",
        )

        ax_ranges.set_title(
            f"Range Count\n{bucket}"
        )

        ax_ranges.set_xlabel("Number of ranges")
        ax_ranges.set_ylabel("Frequency")

        dead_area = (
            100.0 -
            subset["usable_area_pct"]
        )

        ax_dead.hist(
            dead_area,
            bins=15,
            edgecolor="black",
        )

        ax_dead.set_title(
            f"Dead Area\n{bucket}"
        )

        ax_dead.set_xlabel("Dead area (%)")
        ax_dead.set_ylabel("Frequency")

        ax_time.hist(
            subset["exec_time_us"],
            bins=15,
            edgecolor="black",
        )

        ax_time.set_title(
            f"Execution Time\n{bucket}"
        )

        ax_time.set_xlabel("Time (µs)")
        ax_time.set_ylabel("Frequency")

    fig.suptitle(
        "Spatial_Z Range Generation Benchmark",
        fontsize=16,
        fontweight="bold",
    )

    output_path = os.path.join(
        os.path.dirname(os.path.dirname(__file__)),
        "logs",
        "benchmark_summary.png",
    )

    fig.savefig(
        output_path,
        dpi=150,
        bbox_inches="tight",
    )

    print()
    print(f"Plot saved to: {output_path}")

    plt.show()


def main():
    base_dir = run_benchmark()

    results_path = os.path.join(
        base_dir,
        "logs",
        "benchmark_results.csv",
    )

    failure_path = os.path.join(
        base_dir,
        "logs",
        "failure_log.csv",
    )

    if not os.path.exists(results_path):
        print("Benchmark results were not generated.")
        sys.exit(1)

    df = pd.read_csv(results_path)

    failure_df = None

    if os.path.exists(failure_path):
        failure_df = pd.read_csv(failure_path)

    if df.empty:
        print("Benchmark produced no results.")
        sys.exit(1)

    print_summary(df, failure_df)
    create_plots(df)


if __name__ == "__main__":
    main()
