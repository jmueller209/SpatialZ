#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "../include/spatial_z.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double min;
    double max;
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

static double random_double(double min, double max)
{
    return min + ((double)rand() / (double)RAND_MAX) * (max - min);
}

static double random_unit(void)
{
    return ((double)rand() + 1.0) / ((double)RAND_MAX + 2.0);
}

static void random_lat_lon(double* lat, double* lon)
{
    double u = random_double(-1.0, 1.0);

    *lat = asin(u) * 180.0 / M_PI;
    *lon = random_double(-180.0, 180.0);
}

static void random_point_in_circle(
    double center_lat,
    double center_lon,
    double radius_km,
    double* lat,
    double* lon)
{
    double theta = random_double(0.0, 2.0 * M_PI);
    double radius = radius_km * sqrt(random_unit());

    double dy = radius * sin(theta);
    double dx = radius * cos(theta);

    double km_lat = 111.3195;
    double cos_lat = fabs(cos(center_lat * M_PI / 180.0));
    double km_lon = km_lat * cos_lat;

    if (km_lon < 1e-6)
        km_lon = 1e-6;

    *lat = center_lat + dy / km_lat;
    *lon = center_lon + dx / km_lon;

    while (*lon > 180.0)
        *lon -= 360.0;

    while (*lon < -180.0)
        *lon += 360.0;

    if (*lat > 90.0)
        *lat = 90.0;

    if (*lat < -90.0)
        *lat = -90.0;
}

static bool code_in_ranges(
    uint64_t code,
    const SpatialRange* ranges,
    int count)
{
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

static bool validate_ranges(
    const SpatialRange* ranges,
    int count,
    int max_ranges)
{
    if (count < 0 || count > max_ranges)
        return false;

    for (int i = 0; i < count; i++) {
        if (ranges[i].start_code > ranges[i].end_code)
            return false;

        if (i > 0 &&
            ranges[i].start_code <= ranges[i - 1].end_code)
            return false;
    }

    return true;
}

static double estimate_dead_area(
    const SpatialRange* ranges,
    int count,
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx,
    int samples)
{
    double total_span = 0.0;

    for (int i = 0; i < count; i++)
        total_span +=
            (double)(ranges[i].end_code -
                     ranges[i].start_code) + 1.0;

    if (total_span <= 0.0)
        return 100.0;

    int usable = 0;

    for (int s = 0; s < samples; s++) {
        double target =
            random_double(0.0, total_span);

        double accumulated = 0.0;
        uint64_t code = 0;

        for (int i = 0; i < count; i++) {
            double span =
                (double)(ranges[i].end_code -
                         ranges[i].start_code) + 1.0;

            if (target < accumulated + span) {
                uint64_t offset =
                    (uint64_t)(target - accumulated);

                code =
                    ranges[i].start_code + offset;

                break;
            }

            accumulated += span;
        }

        double lat;
        double lon;

        if (!spatial_decode(code, &lat, &lon, ctx))
            continue;

        double km_lat = ctx.unit_length;
        double km_lon =
            ctx.unit_length *
            fabs(cos(center_lat * M_PI / 180.0));

        if (km_lon < 1e-6)
            km_lon = 1e-6;

        double dy =
            (lat - center_lat) * km_lat;

        double dx =
            (lon - center_lon) * km_lon;

        if (dx * dx + dy * dy <=
            radius_km * radius_km)
            usable++;
    }

    return 100.0 *
           (1.0 - (double)usable /
                  (double)samples);
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
    stats->runs = 0;
    stats->failures = 0;
    stats->zero_ranges = 0;
    stats->malformed_ranges = 0;
    stats->range_sum = 0.0;
    stats->dead_area_sum = 0.0;
    stats->max_dead_area = 0.0;
    stats->time_sum_us = 0.0;
    stats->max_time_us = 0.0;

    SpatialRange* ranges =
        malloc((size_t)max_ranges *
               sizeof(SpatialRange));

    if (!ranges)
        return;

    for (int run = 0; run < runs; run++) {
        double center_lat;
        double center_lon;

        random_lat_lon(
            &center_lat,
            &center_lon);

        double radius =
            random_double(
                bucket->min,
                bucket->max);

        int num_ranges = 0;

        struct timespec start;
        struct timespec end;

        clock_gettime(
            CLOCK_MONOTONIC,
            &start);

        bool success =
            spatial_get_radius_ranges(
                center_lat,
                center_lon,
                radius,
                ranges,
                &num_ranges,
                max_ranges,
                ctx);

        clock_gettime(
            CLOCK_MONOTONIC,
            &end);

        double elapsed =
            (double)(end.tv_sec - start.tv_sec) *
                1e6 +
            (double)(end.tv_nsec - start.tv_nsec) /
                1e3;

        stats->runs++;
        stats->time_sum_us += elapsed;

        if (elapsed > stats->max_time_us)
            stats->max_time_us = elapsed;

        if (!success || num_ranges == 0) {
            stats->zero_ranges++;

            fprintf(
                csv,
                "\"%s\",%.8f,%d,1,100.0,%.6f\n",
                bucket->name,
                radius,
                num_ranges,
                elapsed);

            continue;
        }

        if (!validate_ranges(
                ranges,
                num_ranges,
                max_ranges)) {
            stats->malformed_ranges++;

            fprintf(
                csv,
                "\"%s\",%.8f,%d,1,100.0,%.6f\n",
                bucket->name,
                radius,
                num_ranges,
                elapsed);

            continue;
        }

        stats->range_sum += num_ranges;

        int uncovered = 0;

        for (int s = 0;
             s < coverage_samples;
             s++) {
            double lat;
            double lon;

            random_point_in_circle(
                center_lat,
                center_lon,
                radius,
                &lat,
                &lon);

            uint64_t code =
                spatial_encode(lat, lon, ctx);

            if (!code_in_ranges(
                    code,
                    ranges,
                    num_ranges)) {
                uncovered++;
            }
        }

        double uncovered_pct =
            100.0 *
            (double)uncovered /
            (double)coverage_samples;

        bool coverage_failure =
            uncovered > 0;

        if (coverage_failure) {
            stats->failures++;

            fprintf(
                failure_csv,
                "\"%s\",%.8f,%.8f,%.8f,%.4f\n",
                bucket->name,
                center_lat,
                center_lon,
                radius,
                uncovered_pct);
        }

        double dead_area =
            estimate_dead_area(
                ranges,
                num_ranges,
                center_lat,
                center_lon,
                radius,
                ctx,
                dead_area_samples);

        stats->dead_area_sum += dead_area;

        if (dead_area > stats->max_dead_area)
            stats->max_dead_area = dead_area;

        fprintf(
            csv,
            "\"%s\",%.8f,%d,%d,%.6f,%.6f\n",
            bucket->name,
            radius,
            num_ranges,
            coverage_failure ? 1 : 0,
            100.0 - dead_area,
            elapsed);
    }

    free(ranges);
}

int main(void)
{
    srand((unsigned int)time(NULL));

    SpatialzCtx ctx = {
        .min_lat = -90.0,
        .max_lat = 90.0,
        .min_long = -180.0,
        .max_long = 180.0,
        .unit_length = 111.3195
    };

    const RadiusBucket buckets[] = {
        {0.1, 10.0, "Normal (0.1-10 km)"},
        {10.0, 100.0, "Medium (10-100 km)"},
        {100.0, 1000.0, "Large (100-1,000 km)"},
        {1000.0, 5000.0, "Very Large (1,000-5,000 km)"},
        {5000.0, 10000.0, "Macro (5,000-10,000 km)"},
        {10000.0, 20000.0, "Extreme (10,000-20,000 km)"},
        {20000.0, 40000.0, "Global (20,000-40,000 km)"}
    };

    const int bucket_count =
        sizeof(buckets) / sizeof(buckets[0]);

    const int runs = 100;
    const int max_ranges = 8;
    const int coverage_samples = 500;
    const int dead_area_samples = 1000;

    FILE* csv =
        fopen("logs/benchmark_results.csv", "w");

    if (!csv) {
        fprintf(stderr,
                "Failed to open logs/benchmark_results.csv\n");
        return 1;
    }

    FILE* failure_csv =
        fopen("logs/failure_log.csv", "w");

    if (!failure_csv) {
        fclose(csv);
        fprintf(stderr,
                "Failed to open logs/failure_log.csv\n");
        return 1;
    }

    fprintf(
        csv,
        "bucket,radius,num_ranges,coverage_failure,"
        "usable_area_pct,exec_time_us\n");

    fprintf(
        failure_csv,
        "bucket,center_lat,center_lon,"
        "radius,uncovered_pct\n");

    printf(
        "Starting benchmark "
        "(%d runs per bucket, %d max ranges)\n\n",
        runs,
        max_ranges);

    for (int i = 0; i < bucket_count; i++) {
        BucketStats stats;

        run_bucket(
            &buckets[i],
            runs,
            max_ranges,
            coverage_samples,
            dead_area_samples,
            ctx,
            &stats,
            csv,
            failure_csv);

        double avg_ranges =
            stats.runs > 0
                ? stats.range_sum /
                  (double)stats.runs
                : 0.0;

        double avg_dead =
            stats.runs > 0
                ? stats.dead_area_sum /
                  (double)stats.runs
                : 0.0;

        double avg_time =
            stats.runs > 0
                ? stats.time_sum_us /
                  (double)stats.runs
                : 0.0;

        printf(
            "%-30s | %4ld runs | "
            "%3ld failures | "
            "%3ld zero | "
            "%3ld malformed | "
            "%6.2f ranges | "
            "%7.2f%% dead | "
            "%9.2f us avg | "
            "%9.2f us max\n",
            buckets[i].name,
            stats.runs,
            stats.failures,
            stats.zero_ranges,
            stats.malformed_ranges,
            avg_ranges,
            avg_dead,
            avg_time,
            stats.max_time_us);
    }

    fclose(csv);
    fclose(failure_csv);

    printf("\nBenchmark complete.\n");
    printf("Results: logs/benchmark_results.csv\n");
    printf("Failures: logs/failure_log.csv\n");

    return 0;
}
