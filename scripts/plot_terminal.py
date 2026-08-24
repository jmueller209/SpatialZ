import subprocess
import os
import sys
import csv
import numpy as np
import matplotlib.pyplot as plt


def compile_c_code():
    print("Compiling C code...")
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    build_dir = os.path.join(base_dir, "build")
    os.makedirs(build_dir, exist_ok=True)
    exe_name = "test_ranges.exe" if os.name == "nt" else "test_ranges"
    out_path = os.path.join(build_dir, exe_name)

    sources = [
        os.path.join(base_dir, "tests", "test_ranges.c"),
        os.path.join(base_dir, "src", "codec.c"),
        os.path.join(base_dir, "src", "distances.c"),
        os.path.join(base_dir, "src", "ranges.c"),
        os.path.join(base_dir, "src", "utils.c"),
        os.path.join(base_dir, "src", "context.c"),
    ]

    compile_cmd = ["gcc"] + sources + ["-o", out_path, "-lm"]

    try:
        result = subprocess.run(compile_cmd, capture_output=True, text=True, check=True)
        print(f"Compilation successful: {out_path}")
        return out_path, base_dir
    except subprocess.CalledProcessError as e:
        print("Compilation failed!")
        print("STDOUT:", e.stdout)
        print("STDERR:", e.stderr)
        sys.exit(1)


def generate_true_circle(center_lat, center_lon, radius_km, num_points=100):
    R_EARTH = 6371.0
    lat1 = np.radians(center_lat)
    lon1 = np.radians(center_lon)
    bearings = np.radians(np.linspace(0, 360, num_points))
    angular_distance = radius_km / R_EARTH

    lat2 = np.arcsin(
        np.sin(lat1) * np.cos(angular_distance)
        + np.cos(lat1) * np.sin(angular_distance) * np.cos(bearings)
    )
    lon2 = lon1 + np.arctan2(
        np.sin(bearings) * np.sin(angular_distance) * np.cos(lat1),
        np.cos(angular_distance) - np.sin(lat1) * np.sin(lat2),
    )
    return np.degrees(lat2), np.degrees(lon2)


def generate_approx_circle(center_lat, center_lon, radius_km, num_points=100):
    UNIT_LENGTH = 111.3195
    theta = np.linspace(0, 2 * np.pi, num_points)
    dx_km = radius_km * np.cos(theta)
    dy_km = radius_km * np.sin(theta)

    lat2 = center_lat + (dy_km / UNIT_LENGTH)
    cos_lat = np.cos(np.radians(center_lat))
    lon2 = center_lon + (dx_km / (UNIT_LENGTH * cos_lat))
    return lat2, lon2


def run_and_load(exe_path, base_dir, lat, lon, radius, max_ranges):
    result = subprocess.run(
        [exe_path, str(lat), str(lon), str(radius), str(int(max_ranges))],
        capture_output=True,
        text=True,
        cwd=base_dir,
    )

    if "SUCCESS" not in result.stdout:
        print("Error executing C program:")
        print(result.stderr or result.stdout)
        return [], [], []

    lats, lons, range_ids = [], [], []
    csv_path = os.path.join(base_dir, "logs", "ranges_output.csv")

    try:
        with open(csv_path, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row["type"] == "point":
                    lats.append(float(row["lat"]))
                    lons.append(float(row["lon"]))
                    range_ids.append(int(row["range_id"]))
    except FileNotFoundError:
        print("CSV file not found!")

    return lats, lons, range_ids


if __name__ == "__main__":
    EXE_PATH, BASE_DIR = compile_c_code()

    print("\n" + "=" * 40)
    print(" PARAMETER INPUT (TERMINAL)")
    print("=" * 40)

    try:
        lat = float(input("Center Latitude (-90 to 90): "))
        lon = float(input("Center Longitude (-180 to 180): "))
        rad = float(input("Radius in km: "))
        m_ranges = int(input("Max Ranges (e.g. 4 or 16): "))
    except ValueError:
        print("Invalid input! Please enter numeric values.")
        sys.exit(1)

    print("\nGenerating Plot...")
    lats, lons, range_ids = run_and_load(EXE_PATH, BASE_DIR, lat, lon, rad, m_ranges)

    if not lats:
        print("No points found or execution error.")
        sys.exit(1)

    fig, ax = plt.subplots(figsize=(10, 8))

    ax.scatter(
        lons,
        lats,
        c=range_ids,
        cmap="tab20",
        s=2,
        alpha=0.8,
        label="Morton Z-Curve Points",
    )

    circle_lats, circle_lons = generate_true_circle(lat, lon, rad)
    ax.plot(
        circle_lons,
        circle_lats,
        color="red",
        linewidth=2,
        label="True Search Radius (Sphere)",
    )

    approx_lats, approx_lons = generate_approx_circle(lat, lon, rad)
    ax.plot(
        approx_lons,
        approx_lats,
        color="green",
        linestyle="--",
        linewidth=2,
        label="Approximate Circle (Flat)",
    )

    ax.plot(lon, lat, "kx", markersize=10, label="Center")

    ax.set_title(
        f"Center: ({lat}, {lon}) | Radius: {rad:.1f}km | Max Ranges: {m_ranges} | Generated: {max(range_ids)+1 if range_ids else 0}"
    )
    ax.set_xlabel("Longitude")
    ax.set_ylabel("Latitude")
    ax.legend(loc="upper right")

    aspect_ratio = 1.0 / np.cos(np.radians(lat))
    ax.set_aspect(aspect_ratio)

    plt.show()
