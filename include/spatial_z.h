#ifndef SPATIAL_Z_H
#define SPATIAL_Z_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    double min_lat;
    double max_lat;
    double min_long;
    double max_long;
    double unit_length; // distance between 1 degree of latitude: Used for calculating distances
} SpatialzCtx;

typedef struct {
    double top_left_lat;
    double top_left_lon;
    double bottom_right_lat;
    double bottom_right_long;
}Box; 

typedef struct {
    uint64_t start_code;
    uint64_t end_code;
} SpatialRange;

// Encodes lat/lon into a 64-bit Morton code using the context bounds
uint64_t spatial_encode(double lat, double lon, SpatialzCtx ctx);

// Decodes a 64-bit Morton code back into lat/lon 
bool spatial_decode(uint64_t code, double* out_lat, double* out_long, SpatialzCtx ctx);

// Calculates 1D Morton code ranges for a radius search
bool spatial_get_radius_ranges(double center_lat, double center_lon, double radius_km, 
                               SpatialRange* out_ranges, int* num_ranges, int max_ranges, 
                               SpatialzCtx ctx);

// Calculate spatial distance between 2 morton Codes
uint64_t spatial_get_distance(uint64_t code_1, uint64_t code_2, SpatialzCtx ctx);

#endif // SPATIAL_Z_H
