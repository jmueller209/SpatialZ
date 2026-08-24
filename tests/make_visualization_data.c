#include "../include/spatial_z.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
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
    if (argc != 6) {
        printf("Usage: %s <earth|celestial> <y_coord> <x_coord> <radius> <max_ranges>\n", argv[0]);
        printf("       y_coord: Latitude (Earth) or Declination (Celestial)\n");
        printf("       x_coord: Longitude (Earth) or Right Ascension (Celestial)\n");
        printf("       radius : km (Earth) or Degrees (Celestial)\n");
        return 1;
    }

    const char* context_type = argv[1];
    double center_y = atof(argv[2]); // lat / dec
    double center_x = atof(argv[3]); // lon / ra
    double radius   = atof(argv[4]); // km  / deg
    int max_ranges  = atoi(argv[5]);

    SpatialzCtx ctx;
    bool is_earth = true;

    if (strcmp(context_type, "earth") == 0) {
        ctx = spatial_create_earth_ctx();
        is_earth = true;
    } else if (strcmp(context_type, "celestial") == 0) {
        ctx = spatial_create_celestial_ctx();
        is_earth = false;
    } else {
        printf("Error: Unknown context '%s'. Use 'earth' or 'celestial'.\n", context_type);
        return 1;
    }

    MortonRange* ranges = (MortonRange*)malloc(max_ranges * sizeof(MortonRange));
    if (!ranges) {
        printf("Memory allocation failed.\n");
        return 1;
    }

    int num_ranges = 0;

    bool success = spatial_get_radius_ranges(center_y, center_x, radius, 
                                             ranges, &num_ranges, max_ranges, &ctx);

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

    if (is_earth) {
        fprintf(f, "type,lat,lon,range_id\n");
    } else {
        fprintf(f, "type,dec,ra,range_id\n");
    }

    for (int i = 0; i < num_ranges; i++) {
        uint64_t start = ranges[i].start_code;
        uint64_t end = ranges[i].end_code;

        bool printed_any = false;

        for (uint64_t code = start; code <= end; code += global_step) {
            float out_y = 0.0;
            float out_x = 0.0;

            if (spatial_decode(code, &out_y, &out_x, ctx)) {
                fprintf(f, "point,%f,%f,%d\n", out_y, out_x, i);
                printed_any = true;
            }

            if (code > UINT64_MAX - global_step) break; 
        }
        if (!printed_any) {
            float out_y = 0.0;
            float out_x = 0.0;
            if (spatial_decode(start, &out_y, &out_x, ctx)) {
                fprintf(f, "point,%f,%f,%d\n", out_y, out_x, i);
            }
        }
    }

    fclose(f);
    free(ranges);
    printf("SUCCESS:%d\n", num_ranges);
    return 0;
}
