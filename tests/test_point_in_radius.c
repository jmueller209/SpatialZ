#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include "../include/spatial_z.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float randf(float min, float max)
{
    float scale = rand() / (float)RAND_MAX;
    return min + scale * (max - min);
}

static float oracle_surface_distance(float a1, float a2, float b1, float b2, SpatialzCtx ctx)
{
    float lat1 = (a1 - ctx.min_axis1) - 90.0f;
    float lon1 = (a2 - ctx.min_axis2); 

    float lat2 = (b1 - ctx.min_axis1) - 90.0f;
    float lon2 = (b2 - ctx.min_axis2);

    float rlat1 = lat1 * (M_PI / 180.0f);
    float rlon1 = lon1 * (M_PI / 180.0f);
    float rlat2 = lat2 * (M_PI / 180.0f);
    float rlon2 = lon2 * (M_PI / 180.0f);

    float v1[3] = { cosf(rlat1)*cosf(rlon1), cosf(rlat1)*sinf(rlon1), sinf(rlat1) };
    float v2[3] = { cosf(rlat2)*cosf(rlon2), cosf(rlat2)*sinf(rlon2), sinf(rlat2) };

    float dx = v1[0] - v2[0];
    float dy = v1[1] - v2[1];
    float dz = v1[2] - v2[2];

    float chord = sqrtf(dx*dx + dy*dy + dz*dz);
    float angle_rad = 2.0f * asinf(chord * 0.5f);

    float sphere_radius = ctx.unit_length * (180.0f / (float)M_PI);
    return angle_rad * sphere_radius;
}

void run_point_in_radius_test(int num_iterations)
{
    printf("Starting spatial_code_is_in_radius test with RANDOM CONTEXTS (%d iterations)...\n", num_iterations);

    srand(time(NULL));

    int passed = 0;
    int failed = 0;

    for (int i = 0; i < num_iterations; i++) {

        float random_min_a1 = randf(-1000.0f, 1000.0f);
        float random_min_a2 = randf(-1000.0f, 1000.0f);
        float random_unit_length = randf(0.1f, 1000.0f); 

        SpatialzCtx ctx = spatial_create_ctx(random_min_a1, random_min_a2, random_unit_length);

        float center_a1 = randf(ctx.min_axis1 + 10.0f, ctx.min_axis1 + 170.0f);
        float center_a2 = randf(ctx.min_axis2, ctx.min_axis2 + 360.0f);

        float max_surface_dist = ctx.unit_length * 180.0f;
        float radius = randf(max_surface_dist * 0.01f, max_surface_dist * 0.25f);

        CompareCtx comp_ctx = spatial_create_compare_ctx(center_a1, center_a2, radius, ctx);

        bool want_inside = (rand() % 2 == 0);
        float test_a1, test_a2, true_dist;

        while (true) {
            if (want_inside) {
                float degree_offset = radius / ctx.unit_length; 
                test_a1 = center_a1 + randf(-degree_offset, degree_offset);
                test_a2 = center_a2 + randf(-degree_offset, degree_offset);
            } else {
                test_a1 = randf(ctx.min_axis1, ctx.min_axis1 + 180.0f);
                test_a2 = randf(ctx.min_axis2, ctx.min_axis2 + 360.0f);
            }

            if (test_a1 < ctx.min_axis1) test_a1 = ctx.min_axis1;
            if (test_a1 > ctx.min_axis1 + 180.0f) test_a1 = ctx.min_axis1 + 180.0f;

            true_dist = oracle_surface_distance(center_a1, center_a2, test_a1, test_a2, ctx);

            if (want_inside && true_dist < (radius * 0.99f)) break;
            if (!want_inside && true_dist > (radius * 1.01f)) break;
        }

        uint64_t code = spatial_encode(test_a1, test_a2, ctx);
        float result = spatial_code_is_in_radius(code, &comp_ctx);

        bool is_inside_according_to_func = (result >= 0.0f);

        if (is_inside_according_to_func == want_inside) {
            passed++;
        } else {
            failed++;
            printf("\n[ERROR] Iteration %d failed!\n", i);
            printf("  Context:       min_a1 %.2f, min_a2 %.2f, unit_len %.2f\n", ctx.min_axis1, ctx.min_axis2, ctx.unit_length);
            printf("  Center:        Axis1 %.4f, Axis2 %.4f\n", center_a1, center_a2);
            printf("  Test point:    Axis1 %.4f, Axis2 %.4f\n", test_a1, test_a2);
            printf("  Radius:        %.2f units\n", radius);
            printf("  True distance: %.2f units\n", true_dist);
            printf("  Expected:      %s\n", want_inside ? "INSIDE (>= 0.0)" : "OUTSIDE (-1.0)");
            printf("  Function gave: %.6f\n", result);
            break;
        }
    }

    if (failed == 0) {
        printf("[SUCCESS] All %d arbitrary context points were classified 100%% correctly!\n", num_iterations);
    } else {
        printf("[FAILURE] %d succeeded, %d failed.\n", passed, failed);
    }
}

int main(void)
{
    run_point_in_radius_test(100000);
    return 0;
}
