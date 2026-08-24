#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include "../include/spatial_z.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

float random_float(float min, float max) {
    return min + ((float)rand() / RAND_MAX) * (max - min);
}

int main() {
    srand((unsigned int)time(NULL));

    printf("=== RUNNING SPATIAL_Z GENERIC ROUND-TRIP TEST ===\n\n");

    SpatialzCtx ctx = spatial_create_ctx(-90, 0, 1.0);

    float max_axis1 = ctx.min_axis1 + 180.0;
    float max_axis2 = ctx.min_axis2 + 360.0;

    float tolerance_units = 0.001; 
    int total_tests = 10000000;
    int failed_tests = 0;
    int decode_failures = 0;

    float max_error_units = 0.0;

    clock_t start_time = clock();

    for (int i = 0; i < total_tests; i++) {
        float orig_axis1 = random_float(ctx.min_axis1, max_axis1);
        float orig_axis2 = random_float(ctx.min_axis2, max_axis2);

        uint64_t code = spatial_encode(orig_axis1, orig_axis2, ctx);

        float decoded_axis1 = 0.0;
        float decoded_axis2 = 0.0;
        bool success = spatial_decode(code, &decoded_axis1, &decoded_axis2, ctx);

        if (!success) {
            decode_failures++;
            failed_tests++;
            continue;
        }

        float dy = (decoded_axis1 - orig_axis1) * ctx.unit_length;
        float mean_axis1 = 0.5 * (orig_axis1 + decoded_axis1);
        float true_lat = mean_axis1 - ctx.min_axis1 - 90.0;
        float raw_dx = decoded_axis2 - orig_axis2;
        if (raw_dx > 180.0) raw_dx -= 360.0;
        if (raw_dx < -180.0) raw_dx += 360.0;

        float dx = raw_dx * ctx.unit_length * cos(true_lat * (M_PI / 180.0));
        float error_units = sqrt(dx * dx + dy * dy);

        if (error_units > max_error_units) {
            max_error_units = error_units;
        }

        if (error_units > tolerance_units) {
            failed_tests++;
            if (failed_tests <= 10) {
                printf("[TOLERANCE EXCEEDED] Iteration %d:\n", i);
                printf("  Original: axis1=%.6f, axis2=%.6f\n", orig_axis1, orig_axis2);
                printf("  Decoded:  axis1=%.6f, axis2=%.6f\n", decoded_axis1, decoded_axis2);
                printf("  Error:    %.6f units (limit: %.6f units)\n", error_units, tolerance_units);
            }
        }
    }

    clock_t end_time = clock();
    float cpu_time_used = ((float)(end_time - start_time)) / CLOCKS_PER_SEC;

    printf("\n=== TEST SUMMARY OVERVIEW ===\n");
    printf("Total Tests Run:    %d\n", total_tests);
    printf("Passed:             %d\n", total_tests - failed_tests);
    printf("Failed:             %d\n", failed_tests);
    printf("Decode Failures:    %d\n", decode_failures);
    printf("Max Physical Error: %.8f units\n", max_error_units);
    printf("Tolerance Limit:    %.8f units\n", tolerance_units);
    printf("Execution Time:     %.4f seconds\n", cpu_time_used);
    if (failed_tests == 0) {
        printf("\nRESULT: ALL GENERIC ROUND-TRIP TESTS PASSED SUCCESSFULLY!\n");
        return 0;
    } else {
        printf("\nRESULT: SOME TESTS FAILED PHYSICAL TOLERANCE CHECKS.\n");
        return 1;
    }
}
