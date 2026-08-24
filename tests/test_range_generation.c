#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include "../include/spatial_z.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TOTAL_ITERATIONS 100000
#define POINTS_PER_ITER 100
#define MAX_RANGES 128

double random_double(double min, double max) {
    return min + ((double)rand() / RAND_MAX) * (max - min);
}

double dumb_slow_oracle_distance(double y1_deg, double x1_deg, 
                                 double y2_deg, double x2_deg, 
                                 SpatialzCtx ctx) {
    double lat1 = (y1_deg - ctx.min_axis1 - 90.0) * (M_PI / 180.0);
    double lat2 = (y2_deg - ctx.min_axis1 - 90.0) * (M_PI / 180.0);
    double dlat = lat2 - lat1;

    double raw_dlon = x2_deg - x1_deg;
    if (raw_dlon > 180.0) raw_dlon -= 360.0;
    if (raw_dlon < -180.0) raw_dlon += 360.0;
    double dlon = raw_dlon * (M_PI / 180.0);

    double a = sin(dlat / 2.0) * sin(dlat / 2.0) +
               cos(lat1) * cos(lat2) *
               sin(dlon / 2.0) * sin(dlon / 2.0);

    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    double R = (180.0 * ctx.unit_length) / M_PI;

    return R * c;
}

int main() {
    srand((unsigned int)time(NULL));
    printf("=== RUNNING SPATIAL_Z RANGE ORACLE TEST ===\n\n");

    SpatialzCtx ctx = spatial_create_earth_ctx();

    double min_axis1 = ctx.min_axis1;
    double max_axis1 = ctx.min_axis1 + 180.0;
    double min_axis2 = ctx.min_axis2;
    double max_axis2 = ctx.min_axis2 + 360.0;

    double mid_axis1 = ctx.min_axis1 + 90.0;

    int fatal_failures = 0;
    MortonRange ranges[MAX_RANGES];

    int log_interval = TOTAL_ITERATIONS / 100;
    if (log_interval == 0) log_interval = 1;

    for (int i = 0; i < TOTAL_ITERATIONS; i++) {

        if (i > 0 && i % log_interval == 0) {
            int percent = (i * 100) / TOTAL_ITERATIONS;
            printf("[PROGRESS] %d / %d iterations completed (%d%%). Current fatal failures: %d\n", 
                   i, TOTAL_ITERATIONS, percent, fatal_failures);
        }

        double center_y, center_x;

        int bias = rand() % 100;

        if (bias < 10) {
            center_y = min_axis1;
            center_x = random_double(min_axis2, max_axis2);
        } else if (bias < 20) {
            center_y = max_axis1;
            center_x = random_double(min_axis2, max_axis2);
        } else if (bias < 30) {
            double u = random_double(-1.0, 1.0);
            center_y = asin(u) * (180.0 / M_PI) + mid_axis1;
            center_x = min_axis2;
        } else if (bias < 40) {
            double u = random_double(-1.0, 1.0);
            center_y = asin(u) * (180.0 / M_PI) + mid_axis1;
            center_x = max_axis2;
        } else {
            double u = random_double(-1.0, 1.0);
            center_y = asin(u) * (180.0 / M_PI) + mid_axis1;
            center_x = random_double(min_axis2, max_axis2);
        }

        double max_radius = 180.0 * ctx.unit_length;
        double radius = 0.0;

        int rad_bias = rand() % 100;
        if (rad_bias < 5) {
            radius = 0.0;
        } else if (rad_bias < 40) {
            radius = random_double(0.0001, 90.0 * ctx.unit_length);
        } else {
            radius = random_double(90.0 * ctx.unit_length, max_radius);
        }

        int num_ranges = 0;
        bool success = spatial_get_radius_ranges(center_y, center_x, radius, ranges, &num_ranges, MAX_RANGES, &ctx);

        if (!success || num_ranges == 0) {
            printf("[GENERATION FAIL] Iteration %d\n", i);
            printf("  -> Center: y = %.8f, x = %.8f\n", center_y, center_x);
            printf("  -> Radius: %.8f units (Max sphärisch: %.8f)\n", radius, max_radius);
            printf("  -> Status: success=%s, num_ranges=%d\n", success ? "true" : "false", num_ranges);
            fatal_failures++;

            if (fatal_failures >= 20) {
                printf("Too many range generation failures. Aborting test to inspect logs.\n");
                return 1;
            }
            continue;
        }

        for (int p = 0; p < POINTS_PER_ITER; p++) {
            double u = random_double(-1.0, 1.0);
            double test_y = asin(u) * (180.0 / M_PI) + mid_axis1;
            double test_x = random_double(min_axis2, max_axis2);

            double oracle_dist = dumb_slow_oracle_distance(center_y, center_x, test_y, test_x, ctx);
            bool oracle_inside = (oracle_dist <= radius);

            uint64_t test_code = spatial_encode(test_y, test_x, ctx);
            bool code_in_ranges = false;

            for (int r = 0; r < num_ranges; r++) {
                if (test_code >= ranges[r].start_code && test_code <= ranges[r].end_code) {
                    code_in_ranges = true;
                    break;
                }
            }

            if (oracle_inside && !code_in_ranges) {
                printf("[FATAL BUG] Oracle says point is INSIDE, but ranges MISSED it!\n");
                printf("  Center:  y=%.6f, x=%.6f, radius=%.3f\n", center_y, center_x, radius);
                printf("  Test Pt: y=%.6f, x=%.6f\n", test_y, test_x);
                printf("  Oracle Dist: %.3f\n", oracle_dist);
                printf("  Num Ranges Gen: %d\n", num_ranges);
                fatal_failures++;
                if (fatal_failures >= 10) {
                    printf("Too many failures. Aborting test.\n");
                    return 1;
                }
            }
        }
    }

    if (fatal_failures == 0) {
        printf("\nRESULT: SUCCESS! Range Oracle Test passed with 0 false negatives.\n");
        printf("Tested %d random query areas with %d points each (Total %d million points tested).\n", 
               TOTAL_ITERATIONS, POINTS_PER_ITER, (TOTAL_ITERATIONS * POINTS_PER_ITER) / 1000000);
        return 0;
    } else {
        printf("\nRESULT: FAILED. Found %d fatal false negatives.\n", fatal_failures);
        return 1;
    }
}
