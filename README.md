# Spatial_Z

A lightweight, high-performance C99 library for geographic Z-order (Morton) indexing and spatial radius queries. 

Spatial_Z translates 2D geographic circles (latitude, longitude, and radius) into highly optimized 1D integer ranges (Morton codes). This allows databases or embedded systems to perform extremely fast spatial searches using standard 1D indexes (like B-trees) instead of expensive 2D math, while safely handling geospatial edge cases like polar map distortion and International Date Line wrapping.

## Features

*   **Zero Dependencies:** Pure C99, requiring only the standard `<math.h>` library. C++ compatible.
*   **Dual-Mode Math:** Automatically switches between blazing-fast flat-Earth approximations (for small radii) and accurate 3D spherical Haversine math (for massive radii or polar regions).
*   **Quad-Tree Refinement:** Uses a greedy algorithm to build the tightest possible bounding blocks around your search radius, minimizing database "dead area".
*   **Hot-Loop Optimized:** Provides heavily optimized, trigonometry-free distance filtering functions designed to be placed directly into database result loops.

## Project Structure

```text
├── include/
│   └── spatial_z.h         # Public API header
├── src/
│   ├── codec.c             # Morton encoding/decoding
│   ├── context.c           # Configuration contexts
│   ├── distances.c         # Optimized distance and filtering math
│   ├── ranges.c            # Quad-tree radius-to-range generation
│   └── utils.c             # Internal helpers
├── tests/                  # C unit tests and benchmark runners
├── scripts/                # Python scripts for visualization and stats
└── Makefile                # Build system
```

## Building

You can build the static library and run the tests using the provided `Makefile`.

```bash
# Build the static library (libspatial_z.a)
make lib

# Build and run the test suite
make test_codec
make test_range_coverage
```

## Quick Start (Usage)

Here is a standard pipeline for generating ranges and filtering database results.

```c
#include "spatial_z.h"
#include <stdio.h>

int main() {
    SpatialzCtx earth = spatial_create_earth_ctx();

    double search_lat = 53.6;
    double search_lon = 9.47;
    double radius_km = 10.0;
    int max_ranges = 16;
    
    SpatialRange ranges[16];
    int num_ranges = 0;

    if (!spatial_get_radius_ranges(search_lat, search_lon, radius_km, ranges, &num_ranges, max_ranges, earth)) {
        printf("Failed to generate ranges.\n");
        return 1;
    }

    bool is_spherical = (radius_km > 1000.0);
    CompareCtx cmp_ctx = spatial_create_compare_ctx(search_lat, search_lon, radius_km, is_spherical, earth);

    for (int i = 0; i < num_ranges; ++i) {
        
        // --- YOUR DATABASE QUERY GOES HERE ---
        // e.g., SELECT code FROM points WHERE code >= ranges[i].start_code AND code <= ranges[i].end_code;
        
        uint64_t db_code = /* result from DB */;
        
        bool is_inside = is_spherical ? 
            spatial_code_is_in_spherical_radius(db_code, cmp_ctx) :
            spatial_code_is_in_local_radius(db_code, cmp_ctx);

        if (is_inside) {
            // Point is confirmed to be physically inside the circle!
        }
    }

    return 0;
}
```

## Python Visualization Tools

The project includes Python scripts in the `scripts/` folder to visualize how the Z-order blocks wrap around your search radius.

**Requirements:**
```bash
pip install matplotlib pandas numpy
```

**Run the Interactive Plotter:**
Provides a UI with sliders to dynamically change the center coordinates, radius, and `max_ranges` budget to see how the quad-tree adapts in real-time.
```bash
make plot_interactive
```

**Run the Benchmark Stats:**
Compiles the C benchmark, runs statistical analysis, and generates histograms of range counts and precision ("dead area").
```bash
make plot_benchmark
```

## License

This project is licensed under the MIT License.
