import subprocess
import os
import sys
import csv
import numpy as np
import pyvista as pv

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
        subprocess.run(compile_cmd, capture_output=True, text=True, check=True)
        print(f"Compilation successful: {out_path}")
        return out_path, base_dir
    except subprocess.CalledProcessError as e:
        print("Compilation failed!")
        print("STDOUT:", e.stdout)
        print("STDERR:", e.stderr)
        sys.exit(1)


def to_cartesian(lat_deg, lon_deg, r=1.0):
    lat_rad = np.radians(lat_deg)
    lon_rad = np.radians(lon_deg)
    x = r * np.cos(lat_rad) * np.cos(lon_rad)
    y = r * np.cos(lat_rad) * np.sin(lon_rad)
    z = r * np.sin(lat_rad)
    return np.column_stack((x, y, z))


def generate_true_circle_3d(center_y, center_x, angular_radius_deg, num_points=250):
    y1 = np.radians(center_y)
    x1 = np.radians(center_x)
    rad = np.radians(angular_radius_deg)
    bearings = np.linspace(0, 2 * np.pi, num_points)

    y2 = np.arcsin(np.sin(y1) * np.cos(rad) + np.cos(y1) * np.sin(rad) * np.cos(bearings))
    x2 = x1 + np.arctan2(np.sin(bearings) * np.sin(rad) * np.cos(y1),
                         np.cos(rad) - np.sin(y1) * np.sin(y2))

    return to_cartesian(np.degrees(y2), np.degrees(x2), r=1.005)


def run_and_load(exe_path, base_dir, context, y, x, radius_angular, max_ranges):
    if context == "earth":
        rad_val = radius_angular * 111.3195
    else:
        rad_val = radius_angular

    result = subprocess.run(
        [exe_path, context, str(y), str(x), str(rad_val), str(int(max_ranges))],
        capture_output=True,
        text=True,
        cwd=base_dir,
    )

    if "SUCCESS" not in result.stdout:
        return [], [], []

    ys, xs, range_ids = [], [], []
    csv_path = os.path.join(base_dir, "logs", "ranges_output.csv")

    try:
        with open(csv_path, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                if row.get("type") == "point":
                    if context == "earth":
                        ys.append(float(row["lat"]))
                        xs.append(float(row["lon"]))
                    else:
                        ys.append(float(row["dec"]))
                        xs.append(float(row["ra"]))
                    range_ids.append(int(row["range_id"]))
    except FileNotFoundError:
        pass

    return ys, xs, range_ids


if __name__ == "__main__":
    EXE_PATH, BASE_DIR = compile_c_code()
    state = {
        'ctx': 'earth',
        'y': 20.0,
        'x': 45.0,
        'rad': 15.0,
        'ranges': 16
    }

    plotter = pv.Plotter()
    plotter.set_background("white")

    globe = pv.Sphere(radius=0.99, theta_resolution=36, phi_resolution=18)
    plotter.add_mesh(globe, color="whitesmoke", show_edges=True, edge_color="lightgray")

    pts_mesh = pv.PolyData(np.array([[0.0, 0.0, 0.0]]))
    pts_mesh["RangeID"] = np.array([0])

    circle_mesh = pv.lines_from_points(np.zeros((250, 3)), close=True)
    center_mesh = pv.PolyData(np.array([[0.0, 0.0, 0.0]]))

    pts_actor = plotter.add_mesh(
        pts_mesh,
        render_points_as_spheres=True,
        point_size=7,
        scalars="RangeID",
        cmap="tab20",
        show_scalar_bar=False
    )

    plotter.add_mesh(
        circle_mesh,
        color="red",
        line_width=5,
        render_lines_as_tubes=True
    )

    plotter.add_mesh(
        center_mesh,
        color="black",
        render_points_as_spheres=True,
        point_size=12
    )

    def update_viz(*args):
        ys, xs, r_ids = run_and_load(EXE_PATH, BASE_DIR, state['ctx'], state['y'], state['x'], state['rad'], state['ranges'])

        if len(ys) > 0:
            cart_pts = to_cartesian(np.array(ys), np.array(xs), r=1.001)
            new_mesh = pv.PolyData(cart_pts)
            new_mesh["RangeID"] = np.array(r_ids)
            pts_mesh.copy_from(new_mesh)

            max_id = max(r_ids)
            pts_actor.mapper.scalar_range = (0, max_id if max_id > 0 else 1)
        else:
            pts_mesh.copy_from(pv.PolyData())

        circle_mesh.points = generate_true_circle_3d(state['y'], state['x'], state['rad'])

        center_mesh.points = to_cartesian([state['y']], [state['x']], r=1.006)

        unit = "km" if state['ctx'] == "earth" else "deg"
        disp_rad = state['rad'] * 111.3195 if state['ctx'] == "earth" else state['rad']

        plotter.add_text(
            f"Context: {state['ctx'].upper()}\n"
            f"Center: Lat {state['y']:.1f}, Lon {state['x']:.1f}\n"
            f"Radius: {disp_rad:.1f} {unit}\n"
            f"Generated Ranges: {max(r_ids)+1 if r_ids else 0}",
            position="upper_left", 
            font_size=12, 
            color="black", 
            name="info_text"
        )

    def set_y(val): state['y'] = val; update_viz()
    def set_x(val): state['x'] = val; update_viz()
    def set_rad(val): state['rad'] = val; update_viz()
    def set_ranges(val): state['ranges'] = int(val); update_viz()

    plotter.add_slider_widget(set_y, [-90.0, 90.0], value=state['y'], title="Latitude", pointa=(0.02, 0.15), pointb=(0.25, 0.15))
    plotter.add_slider_widget(set_x, [-180.0, 180.0], value=state['x'], title="Longitude", pointa=(0.28, 0.15), pointb=(0.51, 0.15))
    plotter.add_slider_widget(set_rad, [0.1, 120.0], value=state['rad'], title="Angular Radius (Deg)", pointa=(0.54, 0.15), pointb=(0.77, 0.15))
    plotter.add_slider_widget(set_ranges, [4, 128], value=state['ranges'], title="Max Ranges", fmt="%0.0f", pointa=(0.80, 0.15), pointb=(0.98, 0.15))

    def toggle_context(flag):
        state['ctx'] = 'celestial' if flag else 'earth'
        update_viz()

    plotter.add_checkbox_button_widget(toggle_context, value=False, position=(10, 10), size=30, color_on="purple", color_off="green")
    plotter.add_text("Toggle Context (Green=Earth, Purple=Cel)", position=(50, 15), font_size=10, color="black")

    update_viz()

    print("\nControls:")
    print(" - Left Click + Drag: Rotate sphere")
    print(" - Scroll Wheel: Zoom")
    print(" - Shift + Left Click: Pan")

    plotter.show()
