#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>
#include <string.h>

#include "../include/spatial_z.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

#ifndef SPATIALZ_FAST_RADIUS_KM
#define SPATIALZ_FAST_RADIUS_KM 1000.0
#endif
#ifndef SPATIALZ_SPHERICAL_LAT_THRESHOLD_DEG
#define SPATIALZ_SPHERICAL_LAT_THRESHOLD_DEG 75.0
#endif

#define DEG_TO_RAD (M_PI / 180.0)
#define RAD_TO_DEG (180.0 / M_PI)
#define BENCH_RNG_SEED 0x5EED1234ULL
#define COVERAGE_SAMPLES 2000
#define BOUNDARY_SAMPLES 128
#define DEAD_AREA_SAMPLES 5000

static uint64_t rng_state = BENCH_RNG_SEED;

static uint64_t rng_u64(void)
{
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 2685821657736338717ULL;
}

static double rng_unit(void)
{
    return (rng_u64() >> 11) * (1.0 / 9007199254740992.0);
}

static double random_double(double min, double max)
{
    return min + rng_unit() * (max - min);
}

static double clampd(double x, double lo, double hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

static double deg_to_rad(double x) { return x * DEG_TO_RAD; }
static double rad_to_deg(double x) { return x * RAD_TO_DEG; }

static double normalize_lon(double lon)
{
    while (lon < -180.0) lon += 360.0;
    while (lon > 180.0) lon -= 360.0;
    return lon;
}

static double earth_radius_km(SpatialzCtx ctx)
{
    return ctx.unit_length * RAD_TO_DEG;
}

static double angular_distance_rad(
    double lat1,
    double lon1,
    double lat2,
    double lon2)
{
    const double p1 = deg_to_rad(lat1);
    const double p2 = deg_to_rad(lat2);
    const double dl = deg_to_rad(lon2 - lon1);
    const double dp = p2 - p1;

    const double s1 = sin(dp * 0.5);
    const double s2 = sin(dl * 0.5);
    const double a = s1 * s1 + cos(p1) * cos(p2) * s2 * s2;
    return 2.0 * asin(sqrt(clampd(a, 0.0, 1.0)));
}

static bool use_spherical_mode(double center_lat, double radius_km)
{
    return radius_km > SPATIALZ_FAST_RADIUS_KM ||
           fabs(center_lat) >= SPATIALZ_SPHERICAL_LAT_THRESHOLD_DEG;
}

static double distance_km(
    double lat,
    double lon,
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx)
{
    if (!use_spherical_mode(center_lat, radius_km)) {
        const double km_lat = ctx.unit_length;
        const double km_lon = ctx.unit_length * cos(deg_to_rad(center_lat));
        const double dy = (lat - center_lat) * km_lat;
        const double dlon = normalize_lon(lon - center_lon);
        const double dx = dlon * km_lon;
        return sqrt(dx * dx + dy * dy);
    }

    return angular_distance_rad(
        center_lat, center_lon, lat, lon) * earth_radius_km(ctx);
}

static void sample_local_disk(
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx,
    double *lat,
    double *lon)
{
    const double theta = 2.0 * M_PI * rng_unit();
    const double rho = sqrt(rng_unit()) * radius_km;

    const double km_lon = ctx.unit_length * cos(deg_to_rad(center_lat));
    const double safe_lon = fabs(km_lon) < 1e-9 ? 1e-9 : km_lon;

    *lat = center_lat + (rho * sin(theta)) / ctx.unit_length;
    *lon = center_lon + (rho * cos(theta)) / safe_lon;

    *lat = clampd(*lat, ctx.min_lat, ctx.max_lat);
    *lon = normalize_lon(*lon);
}

static void sample_spherical_cap(
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx,
    double *lat,
    double *lon)
{
    const double R = earth_radius_km(ctx);
    const double alpha = fmin(radius_km / R, M_PI);
    const double bearing = 2.0 * M_PI * rng_unit();

    // Uniform on spherical-cap area.
    const double cos_c = 1.0 - rng_unit() * (1.0 - cos(alpha));
    const double c = acos(clampd(cos_c, -1.0, 1.0));

    const double phi1 = deg_to_rad(center_lat);
    const double lam1 = deg_to_rad(center_lon);

    const double sin_phi2 =
        sin(phi1) * cos(c) +
        cos(phi1) * sin(c) * cos(bearing);

    const double phi2 = asin(clampd(sin_phi2, -1.0, 1.0));
    const double y = sin(bearing) * sin(c) * cos(phi1);
    const double x = cos(c) - sin(phi1) * sin(phi2);
    const double lam2 = lam1 + atan2(y, x);

    *lat = clampd(rad_to_deg(phi2), ctx.min_lat, ctx.max_lat);
    *lon = normalize_lon(rad_to_deg(lam2));
}

static void sample_inside(
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx,
    double *lat,
    double *lon)
{
    if (!use_spherical_mode(center_lat, radius_km))
        sample_local_disk(center_lat, center_lon, radius_km, ctx, lat, lon);
    else
        sample_spherical_cap(center_lat, center_lon, radius_km, ctx, lat, lon);
}

static void sample_boundary(
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx,
    int i,
    double *lat,
    double *lon)
{
    const double t = ((double)i + 0.5) / (double)BOUNDARY_SAMPLES;
    const double bearing = 2.0 * M_PI * t;

    if (!use_spherical_mode(center_lat, radius_km)) {
        const double km_lon = ctx.unit_length * cos(deg_to_rad(center_lat));
        const double safe_lon = fabs(km_lon) < 1e-9 ? 1e-9 : km_lon;

        *lat = center_lat + radius_km * sin(bearing) / ctx.unit_length;
        *lon = center_lon + radius_km * cos(bearing) / safe_lon;
        *lat = clampd(*lat, ctx.min_lat, ctx.max_lat);
        *lon = normalize_lon(*lon);
        return;
    }

    const double R = earth_radius_km(ctx);
    const double c = fmin(radius_km / R, M_PI);
    const double phi1 = deg_to_rad(center_lat);
    const double lam1 = deg_to_rad(center_lon);

    const double sin_phi2 =
        sin(phi1) * cos(c) +
        cos(phi1) * sin(c) * cos(bearing);
    const double phi2 = asin(clampd(sin_phi2, -1.0, 1.0));

    const double y = sin(bearing) * sin(c) * cos(phi1);
    const double x = cos(c) - sin(phi1) * sin(phi2);
    const double lam2 = lam1 + atan2(y, x);

    *lat = clampd(rad_to_deg(phi2), ctx.min_lat, ctx.max_lat);
    *lon = normalize_lon(rad_to_deg(lam2));
}

static bool code_in_ranges(uint64_t code, const SpatialRange *ranges, int count)
{
    for (int i = 0; i < count; ++i) {
        if (code >= ranges[i].start_code && code <= ranges[i].end_code)
            return true;
    }
    return false;
}

static bool validate_ranges(const SpatialRange *ranges, int count, int max_ranges)
{
    if (count < 0 || count > max_ranges)
        return false;

    for (int i = 0; i < count; ++i) {
        if (ranges[i].start_code > ranges[i].end_code)
            return false;

        if (i > 0) {
            if (ranges[i - 1].end_code >= ranges[i].start_code)
                return false;
            if (ranges[i - 1].end_code != UINT64_MAX &&
                ranges[i - 1].end_code + 1ULL >= ranges[i].start_code)
                return false;
        }
    }

    return true;
}

static unsigned __int128 range_len128(SpatialRange r)
{
    return ((unsigned __int128)r.end_code -
            (unsigned __int128)r.start_code) + 1U;
}

static unsigned __int128 total_span128(const SpatialRange *ranges, int count)
{
    unsigned __int128 total = 0;
    for (int i = 0; i < count; ++i)
        total += range_len128(ranges[i]);
    return total;
}

static uint64_t sample_code_from_range(SpatialRange r)
{
    const unsigned __int128 len = range_len128(r);
    const unsigned __int128 rnd =
        ((unsigned __int128)rng_u64() << 64) | rng_u64();
    const unsigned __int128 offset = rnd % len;
    return (uint64_t)((unsigned __int128)r.start_code + offset);
}

static double estimate_dead_area_pct(
    const SpatialRange *ranges,
    int num_ranges,
    int samples,
    double center_lat,
    double center_lon,
    double radius_km,
    SpatialzCtx ctx)
{
    if (num_ranges <= 0)
        return 100.0;

    int inside = 0;
    int decoded = 0;

    for (int i = 0; i < num_ranges; ++i) {
        const unsigned __int128 len = range_len128(ranges[i]);
        int n = (int)((unsigned __int128)samples * len /
                      total_span128(ranges, num_ranges));
        if (n < 1) n = 1;

        for (int j = 0; j < n; ++j) {
            const uint64_t code = sample_code_from_range(ranges[i]);
            double lat, lon;
            if (!spatial_decode(code, &lat, &lon, ctx))
                continue;

            ++decoded;
            const double d = distance_km(
                lat, lon,
                center_lat, center_lon,
                radius_km, ctx);

            if (d <= radius_km)
                ++inside;
        }
    }

    if (decoded == 0)
        return 100.0;

    return 100.0 * (1.0 - (double)inside / (double)decoded);
}

typedef struct {
    const char *name;
    double lat_min;
    double lat_max;
    double lon_min;
    double lon_max;
    double radius_min;
    double radius_max;
    int runs;
} Category;

typedef struct {
    int runs;
    int failures;
    int zero_ranges;
    int malformed_ranges;
    int boundary_failures;
    double avg_ranges;
    double avg_dead_area;
    double max_dead_area;
    double avg_time_us;
    double max_time_us;
} Stats;

static Stats run_category(
    const Category *cat,
    int max_ranges,
    SpatialzCtx ctx)
{
    Stats s;
    memset(&s, 0, sizeof(s));
    s.runs = cat->runs;

    long long range_sum = 0;
    double dead_sum = 0.0;
    double time_sum = 0.0;
    int valid = 0;

    for (int run = 0; run < cat->runs; ++run) {
        const double center_lat = random_double(cat->lat_min, cat->lat_max);
        const double center_lon = random_double(cat->lon_min, cat->lon_max);
        const double radius = random_double(cat->radius_min, cat->radius_max);

        SpatialRange ranges[max_ranges];
        int num_ranges = 0;

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
        const bool ok = spatial_get_radius_ranges(
            center_lat, center_lon, radius,
            ranges, &num_ranges, max_ranges, ctx);
        clock_gettime(CLOCK_MONOTONIC_RAW, &t1);

        const double elapsed_us =
            (double)(t1.tv_sec - t0.tv_sec) * 1e6 +
            (double)(t1.tv_nsec - t0.tv_nsec) / 1e3;

        time_sum += elapsed_us;
        if (elapsed_us > s.max_time_us) s.max_time_us = elapsed_us;

        if (!ok || num_ranges == 0) {
            ++s.zero_ranges;
            continue;
        }

        ++valid;
        range_sum += num_ranges;

        if (!validate_ranges(ranges, num_ranges, max_ranges)) {
            ++s.malformed_ranges;
            continue;
        }

        bool failed = false;

        for (int i = 0; i < COVERAGE_SAMPLES; ++i) {
            double lat, lon;
            sample_inside(center_lat, center_lon, radius, ctx, &lat, &lon);

            if (distance_km(lat, lon, center_lat, center_lon, radius, ctx) > radius)
                continue;

            const uint64_t code = spatial_encode(lat, lon, ctx);
            if (!code_in_ranges(code, ranges, num_ranges)) {
                failed = true;
                break;
            }
        }

        for (int i = 0; i < BOUNDARY_SAMPLES && !failed; ++i) {
            double lat, lon;
            sample_boundary(center_lat, center_lon, radius * (1.0 - 1e-8),
                            ctx, i, &lat, &lon);

            const uint64_t code = spatial_encode(lat, lon, ctx);
            if (!code_in_ranges(code, ranges, num_ranges)) {
                failed = true;
                ++s.boundary_failures;
            }
        }

        if (failed)
            ++s.failures;

        const double dead = estimate_dead_area_pct(
            ranges,
            num_ranges,
            DEAD_AREA_SAMPLES,
            center_lat,
            center_lon,
            radius,
            ctx);

        dead_sum += dead;
        if (dead > s.max_dead_area) s.max_dead_area = dead;
    }

    s.avg_ranges = valid ? (double)range_sum / valid : 0.0;
    s.avg_dead_area = valid ? dead_sum / valid : 100.0;
    s.avg_time_us = s.runs ? time_sum / s.runs : 0.0;

    return s;
}

int main(void)
{
    printf("=== SPATIAL_Z RANGE BENCHMARK ===\n");
    printf("Deterministic RNG seed: 0x%llX\n", (unsigned long long)BENCH_RNG_SEED);
    printf("Fast/local radius threshold: %.1f km\n\n", SPATIALZ_FAST_RADIUS_KM);

    SpatialzCtx ctx = {
        .min_lat = -90.0,
        .max_lat = 90.0,
        .min_long = -180.0,
        .max_long = 180.0,
        .unit_length = 111.3195
    };

    const int max_ranges = 32;

    const Category categories[] = {
        {"Normal (0.1-10 km)", -60.0, 60.0, -170.0, 170.0, 0.1, 10.0, 500},
        {"Dateline (50-500 km)", -60.0, 60.0, 175.0, 180.0, 50.0, 500.0, 500},
        {"Polar (50-500 km)", 80.0, 89.9, -180.0, 180.0, 50.0, 500.0, 500},
        {"Large (1,000-3,000 km)", -50.0, 50.0, -170.0, 170.0, 1000.0, 3000.0, 500},
        {"Macro (5,000-8,000 km)", -30.0, 30.0, -170.0, 170.0, 5000.0, 8000.0, 500},
        {"Extreme (10,000-14,000 km)", -10.0, 10.0, -10.0, 10.0, 10000.0, 14000.0, 500}
    };

    const int ncat = (int)(sizeof(categories) / sizeof(categories[0]));

    printf("%-28s | %5s | %8s | %8s | %8s | %10s | %10s | %10s\n",
           "Category", "Runs", "Failures", "Boundary", "AvgRange",
           "DeadArea", "AvgTime", "MaxTime");
    printf("------------------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < ncat; ++i) {
        Stats s = run_category(&categories[i], max_ranges, ctx);

        printf("%-28s | %5d | %8d | %8d | %8.2f | %9.2f%% | %8.2f us | %8.2f us\n",
               categories[i].name,
               s.runs,
               s.failures,
               s.boundary_failures,
               s.avg_ranges,
               s.avg_dead_area,
               s.avg_time_us,
               s.max_time_us);

        printf("    zero=%d malformed=%d max_dead=%.2f%%\n",
               s.zero_ranges,
               s.malformed_ranges,
               s.max_dead_area);
    }

    printf("\nBenchmark complete.\n");
    printf("Coverage rule: every sampled point must map into at least one returned range.\n");
    printf("Dead-area rule: samples are drawn from returned Morton intervals, including [0, UINT64_MAX].\n");

    return 0;
}
