#ifndef SPATIAL_Z_H
#define SPATIAL_Z_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Describes the coordinate system and physical distance model used by
 * the spatial index.
 *
 * The two coordinate axes are angular coordinates measured in degrees.
 *
 * axis1 is the latitude-like coordinate. It always spans exactly 180 
 * degrees starting from min_axis1 (e.g., -90 to +90 for a standard globe).
 *
 * axis2 is the longitude-like periodic coordinate. It always spans exactly 
 * 360 degrees starting from min_axis2 (e.g., -180 to +180, or 0 to 360).
 *
 * The Spatial-Z implementation does not assign any physical meaning to
 * the axes. The caller decides what they represent.
 */
typedef struct {
    /* 
     * Start of the 180-degree interval for the latitude-like axis. 
     */
    float min_axis1;

    /* 
     * Start of the 360-degree interval for the longitude-like axis. 
     */
    float min_axis2;

    /*
     * Physical distance represented by one degree of axis1.
     *
     * The unit is chosen entirely by the caller. It may be kilometres,
     * miles, metres, or any other consistent distance unit.
     *
     * For a spherical surface this value also defines the corresponding
     * surface radius through:
     *
     *     surface_radius = unit_length * 180 / PI
     */
    float unit_length;

    /*
     * Maximum angular query radius for which the fast local approximation
     * is allowed to be used.
     *
     * Queries with a larger angular radius use spherical geometry.
     *
     * Set to 0 to always use spherical geometry.
     */
    float flat_max_radius_deg;
} SpatialzCtx;


/*
 * Identifies the geometric model used by a prepared comparison query.
 */
typedef enum {
    SPATIAL_QUERY_FLAT = 0,
    SPATIAL_QUERY_SPHERICAL = 1
} SpatialQueryMode;


/*
 * Precomputed state for repeatedly testing encoded spatial points against
 * one radius query.
 *
 * The structure is intended for the database search hot path. Expensive
 * values are calculated once when the query starts and then reused for
 * every Morton code that is inspected.
 *
 * The returned distance metric depends on the selected query mode:
 *
 *   FLAT:
 *       squared physical distance in the units defined by SpatialzCtx.
 *
 *   SPHERICAL:
 *       haversine "a" value, which is monotonically increasing with
 *       angular distance.
 *
 * Both metrics can therefore be used for radius filtering and for
 * ordering results within one query.
 */
typedef struct {
    SpatialzCtx spatialCtx;

    float center_axis1;
    float center_axis2;

    float radius;
    float radius_squared;

    float radius_radians;

    SpatialQueryMode mode;

    /*
     * Precomputed scale factors for the local flat approximation.
     *
     * axis1_scale is the physical distance represented by one degree
     * of axis1.
     *
     * axis2_scale is the corresponding local scale for axis2 at the
     * query center.
     */
    float axis1_scale;
    float axis2_scale;

    /*
     * Precomputed values for spherical distance calculations.
     */
    float center_axis1_radians;
    float center_axis2_radians;
    float cos_center_axis1;

    /*
     * Maximum allowed haversine "a" value for the radius.
     *
     * A point is inside the spherical radius when:
     *
     *     haversine_a <= max_haversine_a
     */
    float max_haversine_a;
} CompareCtx;


/*
 * Inclusive interval in 64-bit Morton/Z-order space.
 *
 * Every spatial cell represented by the spatial index is translated into
 * one or more intervals of this type.
 */
typedef struct {
    uint64_t start_code;
    uint64_t end_code;
} MortonRange;


/*
 * Creates a generic spatial context.
 *
 * The caller specifies the minimum angular boundaries (the spans are 
 * strictly fixed to 180 degrees for axis1 and 360 degrees for axis2), 
 * the physical distance represented by one degree of axis1, and the 
 * angular threshold at which spherical geometry replaces the fast local 
 * approximation.
 */
SpatialzCtx spatial_create_ctx(
    float min_axis1,
    float min_axis2,
    float unit_length,
    float flat_max_radius_deg
);

/*
 * Creates a spatial context for geographic coordinates on Earth.
 *
 * Axis 1:
 *   Latitude, starting at -90 degrees (spanning to +90).
 *
 * Axis 2:
 *   Longitude, starting at -180 degrees (spanning to +180).
 *
 * The unit length is approximately 111.3195 km per degree
 * of latitude at the Earth's surface.
 *
 * The local flat-distance approximation is used for query
 * radii up to 5 degrees. Larger radii use spherical geometry.
 */
SpatialzCtx spatial_create_earth_ctx(void);

/*
 * Creates a spatial context for a generic celestial coordinate
 * system using two angular axes.
 *
 * Axis 1:
 *   Declination-like angle, starting at -90 degrees (spanning to +90).
 *
 * Axis 2:
 *   Right-ascension-like angle, starting at 0 degrees (spanning to 360).
 *
 * The unit length is set to 1.0 distance unit per degree.
 * The caller is responsible for interpreting this distance unit.
 *
 * The local flat-distance approximation is used for query
 * radii up to 5 degrees. Larger radii use spherical geometry.
 */
SpatialzCtx spatial_create_celestial_ctx(void);


/*
 * Encodes two angular coordinates into a 64-bit Morton/Z-order code.
 *
 * axis1 is mapped to the first spatial grid dimension.
 *
 * axis2 is mapped to the second spatial grid dimension.
 *
 * The encoding is independent of the physical distance unit and of the
 * geometric interpretation of the two axes. Coordinates exceeding the 
 * standard 180/360 degree bounds are automatically wrapped using pure 
 * spherical geometry before encoding.
 */
uint64_t spatial_encode(
    float axis1,
    float axis2,
    SpatialzCtx ctx
);


/*
 * Decodes a 64-bit Morton/Z-order code back into the two angular
 * coordinates represented by the SpatialzCtx.
 *
 * Returns false when an output pointer is NULL.
 */
bool spatial_decode(
    uint64_t code,
    float *out_axis1,
    float *out_axis2,
    SpatialzCtx ctx
);


/*
 * Generates Morton/Z-order ranges covering a radius query.
 *
 * The returned ranges are guaranteed to represent a conservative spatial
 * cover of the requested query. The function never intentionally removes
 * an intersecting spatial block merely because it is not fully contained
 * by the radius.
 *
 * max_ranges specifies the maximum number of returned ranges.
 *
 * radius is expressed in the physical distance unit defined by
 * SpatialzCtx.unit_length.
 */

#define SPATIALZ_MAX_INTERNAL_BLOCKS 16

bool spatial_get_radius_ranges(
    float center_axis1,
    float center_axis2,
    float radius,
    MortonRange *out_ranges,
    int *out_num_ranges,
    int max_ranges,
    const SpatialzCtx *ctx
);


/*
 * Creates a prepared comparison context for one radius query.
 *
 * The geometric mode is selected automatically from the angular radius
 * and SpatialzCtx.flat_max_radius_deg.
 *
 * No Earth-specific or unit-specific information is required here.
 */
CompareCtx spatial_create_compare_ctx(
    float center_axis1,
    float center_axis2,
    float radius,
    SpatialzCtx spatialCtx
);


/*
 * Tests whether a Morton code lies inside the query radius using the
 * fast local flat approximation.
 *
 * Returns:
 *
 *   >= 0
 *       Squared physical distance from the query center.
 *
 *   -1
 *       The spatial point lies outside the query radius.
 *
 * The returned squared distance uses the physical distance unit defined
 * by SpatialzCtx.unit_length.
 */
float spatial_code_is_in_flat_radius(
    uint64_t code,
    const CompareCtx *ctx
);


/*
 * Tests whether a Morton code lies inside the query radius using spherical
 * geometry.
 *
 * Returns:
 *
 *   >= 0
 *       Haversine "a" value:
 *
 *           sin²(angular_distance / 2)
 *
 *       This value is monotonically increasing with angular distance.
 *
 *   -1
 *       The spatial point lies outside the query radius.
 *
 * The function avoids sqrt() and asin() in the database search hot path.
 */
float spatial_code_is_in_spherical_radius(
    uint64_t code,
    const CompareCtx *ctx
);


/*
 * Tests a Morton code using the query mode prepared in CompareCtx.
 *
 * This is the preferred function for database search code because the
 * caller does not need to know whether the current query uses flat or
 * spherical geometry.
 */
float spatial_code_is_in_radius(
    uint64_t code,
    const CompareCtx *ctx
);

#ifdef __cplusplus
}
#endif

#endif
