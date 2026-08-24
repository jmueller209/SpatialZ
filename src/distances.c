#include "../include/spatial_z.h"
#include "utils.h"
#include <stddef.h>
#include <math.h>

#ifndef SPATIALZ_PI
#define SPATIALZ_PI 3.14159265358979323846
#endif

static inline float deg_to_rad(float value)
{
    return value * (SPATIALZ_PI / 180.0);
}

static inline float rad_to_deg(float value)
{
    return value * (180.0 / SPATIALZ_PI);
}

static inline float clamp_float(
    float value,
    float minimum,
    float maximum)
{
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return value;
}

static inline float surface_radius(
    const SpatialzCtx *ctx)
{
    return ctx->unit_length *
           (180.0 / SPATIALZ_PI);
}

static inline float normalize_axis2(
    float axis2,
    const SpatialzCtx *ctx)
{
    const float period = 360.0;

    float normalized =
        fmod(
            axis2 - ctx->min_axis2,
            period
        );

    if (normalized < 0.0)
        normalized += period;

    return ctx->min_axis2 + normalized;
}

static inline float angular_distance_rad(
    float axis1_a_rad,
    float axis2_a_rad,
    float axis1_b_rad,
    float axis2_b_rad)
{
    const float d_axis1 =
        axis1_b_rad - axis1_a_rad;

    const float d_axis2 =
        axis2_b_rad - axis2_a_rad;

    const float sin_axis1 =
        sin(d_axis1 * 0.5);

    const float sin_axis2 =
        sin(d_axis2 * 0.5);

    const float a =
        sin_axis1 * sin_axis1 +
        cos(axis1_a_rad) *
        cos(axis1_b_rad) *
        sin_axis2 * sin_axis2;

    return 2.0 *
           asin(
               sqrt(
                   clamp_float(
                       a,
                       0.0,
                       1.0
                   )
               )
           );
}

CompareCtx spatial_create_compare_ctx(
    float center_axis1,
    float center_axis2,
    float radius,
    SpatialzCtx spatialCtx)
{
    CompareCtx ctx = {0};

    ctx.spatialCtx = spatialCtx;

    center_axis1 =
        clamp_float(
            center_axis1,
            spatialCtx.min_axis1,
            spatialCtx.min_axis1 + 180.0
        );

    center_axis2 =
        normalize_axis2(
            center_axis2,
            &spatialCtx
        );

    ctx.center_axis1 = center_axis1;
    ctx.center_axis2 = center_axis2;

    ctx.radius = radius;
    ctx.radius_squared = radius * radius;

    ctx.center_axis1_radians =
        deg_to_rad(center_axis1);

    ctx.center_axis2_radians =
        deg_to_rad(center_axis2);

    ctx.cos_center_axis1 =
        cos(ctx.center_axis1_radians);

    ctx.axis1_scale =
        spatialCtx.unit_length;

    ctx.axis2_scale =
        spatialCtx.unit_length *
        ctx.cos_center_axis1;

    const float sphere_radius =
        surface_radius(&spatialCtx);

    ctx.radius_radians =
        sphere_radius > 0.0
            ? radius / sphere_radius
            : 0.0;

    const float radius_degrees =
        rad_to_deg(ctx.radius_radians);

    const bool use_flat =
        spatialCtx.flat_max_radius_deg > 0.0 &&
        radius_degrees <= spatialCtx.flat_max_radius_deg &&
        fabs(ctx.cos_center_axis1) > 1e-12;

    if (use_flat) {
        ctx.mode =
            SPATIAL_QUERY_FLAT;
    } else {
        ctx.mode =
            SPATIAL_QUERY_SPHERICAL;

        const float sin_half_radius =
            sin(ctx.radius_radians * 0.5);

        ctx.max_haversine_a =
            sin_half_radius *
            sin_half_radius;
    }

    return ctx;
}

float spatial_code_is_in_flat_radius(
    uint64_t code,
    const CompareCtx *ctx)
{
    if (ctx == NULL)
        return -1.0;

    float axis1;
    float axis2;

    if (!spatial_decode(
            code,
            &axis1,
            &axis2,
            ctx->spatialCtx)) {

        return -1.0;
    }

    const float d_axis1 =
        axis1 - ctx->center_axis1;

    const float raw_d_axis2 =
        axis2 - ctx->center_axis2;

    const float period = 360.0;

    float d_axis2 = raw_d_axis2;

    d_axis2 =
        fmod(
            raw_d_axis2 + period * 0.5,
            period
        );

    if (d_axis2 < 0.0)
        d_axis2 += period;

    d_axis2 -= period * 0.5;

    const float physical_axis1 =
        d_axis1 * ctx->axis1_scale;

    const float physical_axis2 =
        d_axis2 * ctx->axis2_scale;

    const float distance_squared =
        physical_axis1 * physical_axis1 +
        physical_axis2 * physical_axis2;

    if (distance_squared >
        ctx->radius_squared) {

        return -1.0;
    }

    return distance_squared;
}

float spatial_code_is_in_spherical_radius(
    uint64_t code,
    const CompareCtx *ctx)
{
    if (ctx == NULL)
        return -1.0;

    float axis1;
    float axis2;

    if (!spatial_decode(
            code,
            &axis1,
            &axis2,
            ctx->spatialCtx)) {

        return -1.0;
    }

    axis2 =
        normalize_axis2(
            axis2,
            &ctx->spatialCtx
        );

    const float axis1_rad =
        deg_to_rad(axis1);

    const float axis2_rad =
        deg_to_rad(axis2);

    const float d_axis1 =
        axis1_rad -
        ctx->center_axis1_radians;

    const float d_axis2 =
        axis2_rad -
        ctx->center_axis2_radians;

    const float sin_axis1 =
        sin(d_axis1 * 0.5);

    const float sin_axis2 =
        sin(d_axis2 * 0.5);

    const float a =
        sin_axis1 * sin_axis1 +
        ctx->cos_center_axis1 *
        cos(axis1_rad) *
        sin_axis2 * sin_axis2;

    if (a >
        ctx->max_haversine_a) {

        return -1.0;
    }

    return a;
}

float spatial_code_is_in_radius(
    uint64_t code,
    const CompareCtx *ctx)
{
    if (ctx == NULL)
        return -1.0;

    if (ctx->mode ==
        SPATIAL_QUERY_SPHERICAL) {

        return spatial_code_is_in_spherical_radius(
            code,
            ctx
        );
    }

    return spatial_code_is_in_flat_radius(
        code,
        ctx
    );
}
