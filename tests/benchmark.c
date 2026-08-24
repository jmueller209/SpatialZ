#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <string.h>

#include "../include/spatial_z.h"

#define MAX_RANGES 16

typedef struct {
    double min_deg;
    double max_deg;
    const char* name;
} RadiusBucket;

typedef struct {
    long runs;
    long failures;
    long zero_ranges;
    long malformed_ranges;
    double range_sum;
    double dead_area_sum;
    double max_dead_area;
    double time_sum_us;
    double max_time_us;
} BucketStats;

static double random_double(double min, double max) {
    return min + ((double)rand() / (double)RAND_MAX) * (max - min);
}

static void random_center(SpatialzCtx ctx, double* axis1, double* axis2) {
    double u = random_double(-1.0, 1.0);
    *axis1 = (asin(u) * 180.0 / M_PI) + (ctx.min_axis1 + 90.0);
    *axis2 = random_double(ctx.min_axis2, ctx.min_axis2 + 360.0);
}

static void random_point_in_cap(
    double center_axis1,
    double center_axis2,
    double radius_deg,
    SpatialzCtx ctx,
    double* out_axis1,
    double* out_axis2)
{
    double r_rad = radius_deg * (M_PI / 180.0);

    double z = random_double(cos(r_rad), 1.0);
    double theta = acos(z); 
    double phi = random_double(0.0, 2.0 * M_PI);

    double lat1 = (center_axis1 - ctx.min_axis1 - 90.0) * (M_PI / 180.0);
    double lon1 = (center_axis2 - ctx.min_axis2) * (M_PI / 180.0);

    double lat2 = asin(sin(lat1) * cos(theta) + cos(lat1) * sin(theta) * cos(phi));
    double lon2 = lon1 + atan2(sin(phi) * sin(theta) * cos(lat1), cos(theta) - sin(lat1) * sin(lat2));

    *out_axis1 = (lat2 * (180.0 / M_PI)) + 90.0 + ctx.min_axis1;
    double deg_lon2 = fmod(lon2 * (180.0 / M_PI), 360.0);
    if (deg_lon2 < 0.0) deg_lon2 += 360.0;
    *out_axis2 = deg_lon2 + ctx.min_axis2;
}

static double dumb_slow_oracle_distance(double y1_deg, double x1_deg, 
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

static bool code_in_ranges(uint64_t code, const MortonRange* ranges, int count) {
    int lo = 0;
    int hi = count - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (code < ranges[mid].start_code)
            hi = mid - 1;
        else if (code > ranges[mid].end_code)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

static bool validate_ranges(const MortonRange* ranges, int count, int max_ranges) {
    if (count <= 0 || count > max_ranges) return false;

    for (int i = 0; i < count; i++) {
        if (ranges[i].start_code > ranges[i].end_code) return false;
        if (i > 0 && ranges[i].start_code <= ranges[i - 1].end_code) return false;
    }
    return true;
}

static double estimate_dead_area(
    const MortonRange* ranges,
    int count,
    double center_axis1,
    double center_axis2,
    double radius_deg,
    SpatialzCtx ctx,
    int samples)
{
    double total_span = 0.0;
    for (int i = 0; i < count; i++) {
        total_span += (double)(ranges[i].end_code - ranges[i].start_code) + 1.0;
    }

    if (total_span <= 0.0) return 100.0;

    int usable = 0;

    for (int s = 0; s < samples; s++) {
        double target = random_double(0.0, total_span);
        double accumulated = 0.0;
        uint64_t code = 0;

        for (int i = 0; i < count; i++) {
            double span = (double)(ranges[i].end_code - ranges[i].start_code) + 1.0;
            if (target < accumulated + span) {
                uint64_t offset = (uint64_t)(target - accumulated);
                code = ranges[i].start_code + offset;
                break;
            }
            accumulated += span;
        }

        float pt_axis1, pt_axis2;
        if (!spatial_decode(code, &pt_axis1, &pt_axis2, ctx)) continue;

        double dist = dumb_slow_oracle_distance(center_axis1, center_axis2, pt_axis1, pt_axis2, ctx);
        if (dist <= radius_deg) {
            usable++;
        }
    }

    return 100.0 * (1.0 - (double)usable / (double)samples);
}

static void run_bucket(
    const RadiusBucket* bucket,
    int runs,
    int max_ranges,
    int coverage_samples,
    int dead_area_samples,
    SpatialzCtx ctx,
    BucketStats* stats,
    FILE* csv,
    FILE* failure_csv)
{
    memset(stats, 0, sizeof(BucketStats));
    MortonRange* ranges = malloc((size_t)max_ranges * sizeof(MortonRange));
    if (!ranges) return;

    for (int run = 0; run < runs; run++) {
        double center_axis1, center_axis2;
        random_center(ctx, &center_axis1, &center_axis2);

        double radius = random_double(bucket->min_deg, bucket->max_deg);
        int num_ranges = 0;

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        bool success = spatial_get_radius_ranges(
            center_axis1, center_axis2, radius, 
            ranges, &num_ranges, max_ranges, &ctx
        );

        clock_gettime(CLOCK_MONOTONIC, &end);
        double elapsed = (double)(end.tv_sec - start.tv_sec) * 1e6 +
                         (double)(end.tv_nsec - start.tv_nsec) / 1e3;

        stats->runs++;
        stats->time_sum_us += elapsed;
        if (elapsed > stats->max_time_us) stats->max_time_us = elapsed;

        if (!success || num_ranges == 0) {
            stats->zero_ranges++;
            fprintf(csv, "\"%s\",%.8f,%d,1,100.0,%.6f\n", bucket->name, radius, num_ranges, elapsed);
            printf("[ZERO RANGES] Bucket: %s | Center: y=%.8f, x=%.8f | Radius: %.8f deg\n", 
                   bucket->name, center_axis1, center_axis2, radius);
            continue;
        }

        if (!validate_ranges(ranges, num_ranges, max_ranges)) {
            stats->malformed_ranges++;
            fprintf(csv, "\"%s\",%.8f,%d,1,100.0,%.6f\n", bucket->name, radius, num_ranges, elapsed);
            printf("[MALFORMED] Bucket: %s | Center: y=%.8f, x=%.8f | Radius: %.8f deg\n", 
                   bucket->name, center_axis1, center_axis2, radius);
            continue;
        }

        stats->range_sum += num_ranges;

        int uncovered = 0;
        for (int s = 0; s < coverage_samples; s++) {
            double pt_axis1, pt_axis2;
            random_point_in_cap(center_axis1, center_axis2, radius, ctx, &pt_axis1, &pt_axis2);
            uint64_t code = spatial_encode(pt_axis1, pt_axis2, ctx);
            if (!code_in_ranges(code, ranges, num_ranges)) uncovered++;
        }

        double uncovered_pct = 100.0 * (double)uncovered / (double)coverage_samples;
        bool coverage_failure = uncovered > 0;

        if (coverage_failure) {
            stats->failures++;
            fprintf(failure_csv, "\"%s\",%.8f,%.8f,%.8f,%.4f\n", 
                    bucket->name, center_axis1, center_axis2, radius, uncovered_pct);
            printf("[COVERAGE FAIL] Bucket: %s | Center: y=%.8f, x=%.8f | Radius: %.8f deg | Missed: %.2f%%\n",
                   bucket->name, center_axis1, center_axis2, radius, uncovered_pct);
        }

        double dead_area = estimate_dead_area(
            ranges, num_ranges, center_axis1, center_axis2, radius, ctx, dead_area_samples
        );

        stats->dead_area_sum += dead_area;
        if (dead_area > stats->max_dead_area) stats->max_dead_area = dead_area;

        fprintf(csv, "\"%s\",%.8f,%d,%d,%.6f,%.6f\n",
                bucket->name, radius, num_ranges, coverage_failure ? 1 : 0, 100.0 - dead_area, elapsed);
    }
    free(ranges);
}

int main(void)
{
    srand((unsigned int)time(NULL));

    SpatialzCtx ctx = spatial_create_ctx(-90, 0, 1);

    const RadiusBucket buckets[] = {
        {0.001, 0.05,  "Micro (< 0.05 deg)"},
        {0.05,  1.0,   "Small (0.05 - 1 deg)"},
        {1.0,   5.0,   "Medium (1 - 5 deg)"},
        {5.0,   20.0,  "Large (5 - 20 deg)"},
        {20.0,  90.0,  "Hemisphere (20 - 90 deg)"},
        {90.0,  180.0, "Global (90 - 180 deg)"}
    };

    const int bucket_count = sizeof(buckets) / sizeof(buckets[0]);
    const int runs = 100;
    const int coverage_samples = 500;
    const int dead_area_samples = 1000;

    FILE* csv = fopen("logs/benchmark_results.csv", "w");
    if (!csv) {
        fprintf(stderr, "Failed to open logs/benchmark_results.csv\n");
        return 1;
    }

    FILE* failure_csv = fopen("logs/failure_log.csv", "w");
    if (!failure_csv) {
        fclose(csv);
        fprintf(stderr, "Failed to open logs/failure_log.csv\n");
        return 1;
    }

    fprintf(csv, "bucket,radius_deg,num_ranges,coverage_failure,usable_area_pct,exec_time_us\n");
    fprintf(failure_csv, "bucket,center_axis1,center_axis2,radius_deg,uncovered_pct\n");

    printf("Starting Angle-Based Benchmark (%d runs per bucket, %d max ranges)\n\n", runs, MAX_RANGES);

    for (int i = 0; i < bucket_count; i++) {
        BucketStats stats;

        run_bucket(&buckets[i], runs, MAX_RANGES, coverage_samples, dead_area_samples, 
                   ctx, &stats, csv, failure_csv);

        double avg_ranges = stats.runs > 0 ? stats.range_sum / (double)stats.runs : 0.0;
        double avg_dead = stats.runs > 0 ? stats.dead_area_sum / (double)stats.runs : 0.0;
        double avg_time = stats.runs > 0 ? stats.time_sum_us / (double)stats.runs : 0.0;

        printf("%-26s | %4ld runs | %3ld fail | %3ld zero | %3ld malformed | %5.2f ranges | %6.2f%% dead | %8.2f us avg | %8.2f us max\n",
               buckets[i].name, stats.runs, stats.failures, stats.zero_ranges, stats.malformed_ranges,
               avg_ranges, avg_dead, avg_time, stats.max_time_us);
    }

    fclose(csv);
    fclose(failure_csv);

    printf("\nBenchmark complete.\n");
    return 0;
}
