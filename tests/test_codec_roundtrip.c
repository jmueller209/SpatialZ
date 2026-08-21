#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include "../include/spatial_z.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double random_double(double min, double max) {
    return min + ((double)rand() / RAND_MAX) * (max - min);
}

int main() {
    srand((unsigned int)time(NULL));

    printf("=== RUNNING SPATIAL_Z PHYSICAL-ERROR ROUND-TRIP TEST ===\n\n");

    SpatialzCtx earth_ctx = {
        .min_lat = -90.0,
        .max_lat = 90.0,
        .min_long = -180.0,
        .max_long = 180.0,
        .unit_length = 111.0 
    };

    double tolerance_km = 0.001; 
    int total_tests = 10000000;
    int failed_tests = 0;
    int decode_failures = 0;

    double max_error_km = 0.0;

    clock_t start_time = clock();

    for (int i = 0; i < total_tests; i++) {
        double u = random_double(-1.0, 1.0);
        double original_lat = asin(u) * (180.0 / M_PI);
        double original_lon = random_double(-180.0, 180.0);

        uint64_t code = spatial_encode(original_lat, original_lon, earth_ctx);

        double decoded_lat = 0.0;
        double decoded_lon = 0.0;
        bool success = spatial_decode(code, &decoded_lat, &decoded_lon, earth_ctx);

        if (!success) {
            decode_failures++;
            failed_tests++;
            continue;
        }

        double dy = (decoded_lat - original_lat) * earth_ctx.unit_length;
        double mean_lat = 0.5 * (original_lat + decoded_lat);
        double dx = (decoded_lon - original_lon) * earth_ctx.unit_length * cos(mean_lat * (M_PI / 180.0));
        double error_km = sqrt(dx * dx + dy * dy);

        if (error_km > max_error_km) {
            max_error_km = error_km;
        }

        if (error_km > tolerance_km) {
            failed_tests++;
            printf("[TOLERANCE EXCEEDED] Iteration %d:\n", i);
            printf("  Original: lat=%.6f, lon=%.6f\n", original_lat, original_lon);
            printf("  Decoded:  lat=%.6f, lon=%.6f\n", decoded_lat, decoded_lon);
            printf("  Error:    %.6f km (limit: %.6f km)\n", error_km, tolerance_km);
        }
    }

    clock_t end_time = clock();
    double cpu_time_used = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    printf("\n=== TEST SUMMARY OVERVIEW ===\n");
    printf("Total Tests Run:    %d\n", total_tests);
    printf("Passed:             %d\n", total_tests - failed_tests);
    printf("Failed:             %d\n", failed_tests);
    printf("Decode Failures:    %d\n", decode_failures);
    printf("Max Physical Error: %.8f km\n", max_error_km);
    printf("Tolerance Limit:    %.8f km\n", tolerance_km);
    printf("Execution Time:     %.4f seconds\n", cpu_time_used);
    if (failed_tests == 0) {
        printf("\nRESULT: ALL PHYSICAL ROUND-TRIP TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("\nRESULT: SOME TESTS FAILED PHYSICAL TOLERANCE CHECKS.\n");
        return 1;
    }
}
