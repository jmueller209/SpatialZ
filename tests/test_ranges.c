#include "../include/spatial_z.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
    #include <direct.h>
    #define MAKE_DIR(path) _mkdir(path)
#else
    #define MAKE_DIR(path) mkdir(path, 0777)
#endif

#define TOTAL_SAMPLES 10000

int main(int argc, char** argv) {
    if (argc != 5) {
        printf("Usage: %s <center_lat> <center_lon> <radius_km> <max_ranges>\n", argv[0]);
        return 1;
    }

    double center_lat = atof(argv[1]);
    double center_lon = atof(argv[2]);
    double radius_km  = atof(argv[3]);
    int max_ranges    = atoi(argv[4]);

    SpatialzCtx ctx;
    ctx.min_lat = -90.0;
    ctx.max_lat = 90.0;
    ctx.min_long = -180.0;
    ctx.max_long = 180.0;
    ctx.unit_length = 111.3195;

    SpatialRange* ranges = (SpatialRange*)malloc(max_ranges * sizeof(SpatialRange));
    if (!ranges) {
        printf("Memory allocation failed.\n");
        return 1;
    }

    int num_ranges = 0;

    bool success = spatial_get_radius_ranges(center_lat, center_lon, radius_km, 
                                             ranges, &num_ranges, max_ranges, ctx);

    if (!success || num_ranges == 0) {
        printf("No ranges found.\n");
        free(ranges);
        return 1;
    }

    double total_span = 0.0;
    for (int i = 0; i < num_ranges; i++) {
        total_span += (double)(ranges[i].end_code - ranges[i].start_code) + 1.0;
    }

    uint64_t global_step = (uint64_t)(total_span / TOTAL_SAMPLES);
    if (global_step == 0) global_step = 1;

    MAKE_DIR("logs");
    FILE* f = fopen("logs/ranges_output.csv", "w");
    if (f == NULL) {
        free(ranges);
        return 1;
    }

    fprintf(f, "type,lat,lon,range_id\n");

    for (int i = 0; i < num_ranges; i++) {
        uint64_t start = ranges[i].start_code;
        uint64_t end = ranges[i].end_code;

        bool printed_any = false;

        for (uint64_t code = start; code <= end; code += global_step) {
            double out_lat = 0.0;
            double out_lon = 0.0;

            if (spatial_decode(code, &out_lat, &out_lon, ctx)) {
                fprintf(f, "point,%f,%f,%d\n", out_lat, out_lon, i);
                printed_any = true;
            }

            if (code > UINT64_MAX - global_step) break; 
        }
        if (!printed_any) {
            double out_lat = 0.0;
            double out_lon = 0.0;
            if (spatial_decode(start, &out_lat, &out_lon, ctx)) {
                fprintf(f, "point,%f,%f,%d\n", out_lat, out_lon, i);
            }
        }
    }

    fclose(f);
    free(ranges);
    printf("SUCCESS:%d\n", num_ranges);
    return 0;
}
